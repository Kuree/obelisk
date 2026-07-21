//===- BuildComputeGraph.cpp - Derive late simulation schedule -----------===//
//
// The executable obelisk_sim CFG remains the source of truth. This pass adds
// deterministic compiler metadata only after local SSA optimization: precise
// descriptor-range summaries, fixed static sites, fragment ABI records, and a
// derived event-region graph with SCC convergence groups.
//
//===----------------------------------------------------------------------===//

#include "Detail.h"

#include "obelisk/Conversion/ObeliskToSimulation.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TypeSwitch.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace mlir;

namespace obelisk {

#define GEN_PASS_DEF_OBELISKSIMBUILDCOMPUTEGRAPHPASS
#include "obelisk/Conversion/Passes.h.inc"

namespace {

enum class ResourceKind { Unknown, Storage, Net, Event, Local };
enum class EffectKind { Read, Write, Drive, Watch, NBA, Trigger };

sim::ComputeResourceKind convert(ResourceKind kind) {
  switch (kind) {
  case ResourceKind::Unknown:
    return sim::ComputeResourceKind::Unknown;
  case ResourceKind::Storage:
    return sim::ComputeResourceKind::Storage;
  case ResourceKind::Net:
    return sim::ComputeResourceKind::Net;
  case ResourceKind::Event:
    return sim::ComputeResourceKind::Event;
  case ResourceKind::Local:
    return sim::ComputeResourceKind::Local;
  }
  llvm_unreachable("unknown resource kind");
}

sim::ComputeEffectKind convert(EffectKind kind) {
  switch (kind) {
  case EffectKind::Read:
    return sim::ComputeEffectKind::Read;
  case EffectKind::Write:
    return sim::ComputeEffectKind::Write;
  case EffectKind::Drive:
    return sim::ComputeEffectKind::Drive;
  case EffectKind::Watch:
    return sim::ComputeEffectKind::Watch;
  case EffectKind::NBA:
    return sim::ComputeEffectKind::NBA;
  case EffectKind::Trigger:
    return sim::ComputeEffectKind::Trigger;
  }
  llvm_unreachable("unknown effect kind");
}

struct Provenance {
  ResourceKind kind = ResourceKind::Unknown;
  std::optional<uint64_t> descriptor;
  std::optional<unsigned> formal;
  uint64_t low = 0;
  uint64_t width = 0;
  uint64_t rootWidth = 0;
  bool dynamic = false;
  bool valid = false;

  bool operator==(const Provenance &other) const {
    return kind == other.kind && descriptor == other.descriptor &&
           formal == other.formal && low == other.low && width == other.width &&
           rootWidth == other.rootWidth && dynamic == other.dynamic &&
           valid == other.valid;
  }
};

struct Effect {
  EffectKind kind;
  Provenance target;
  sim::ComputeTriggerKind trigger = sim::ComputeTriggerKind::None;
  bool deferred = false;

  bool operator==(const Effect &other) const {
    return kind == other.kind && target == other.target &&
           trigger == other.trigger && deferred == other.deferred;
  }
};

struct FunctionInfo {
  explicit FunctionInfo(sim::SimFuncOp function) : function(function) {}
  sim::SimFuncOp function;
  DenseMap<Value, Provenance> provenance;
  DenseMap<Value, bool> known;
  SmallVector<Effect> baseEffects;
  SmallVector<Effect> summary;
  SmallVector<sim::SimCallOp> calls;
};

struct Fragment {
  uint64_t id;
  sim::SimFuncOp function;
  Block *block;
  uint64_t ordinal;
  SmallVector<Effect> effects;
  uint64_t cost;
  bool twoState;
  unsigned lane = 0;
};

struct Edge {
  uint64_t source;
  uint64_t target;
  sim::ComputeEdgeKind kind;
  std::optional<Effect> resource;
};

struct IndexedEffect {
  unsigned fragment;
  const Effect *effect;
};

/// Index effects by resource class and concrete descriptor. Formal and unknown
/// handles stay in explicit wildcard buckets, so common closed-world RTL does
/// not require comparing every fragment pair.
class EffectIndex {
public:
  void add(unsigned fragment, const Effect &effect) {
    IndexedEffect indexed{fragment, &effect};
    all.push_back(indexed);
    if (effect.target.kind == ResourceKind::Unknown) {
      unknown.push_back(indexed);
      return;
    }
    unsigned kind = static_cast<unsigned>(effect.target.kind);
    byKind[kind].push_back(indexed);
    if (effect.target.descriptor)
      exact[{kind, *effect.target.descriptor}].push_back(indexed);
    else
      wildcard[kind].push_back(indexed);
  }

  template <typename Callback>
  void forEachAlias(const Provenance &target, Callback &&callback) const {
    auto visit = [&](ArrayRef<IndexedEffect> effects) {
      for (IndexedEffect effect : effects)
        callback(effect);
    };
    if (target.kind == ResourceKind::Unknown) {
      visit(all);
      return;
    }
    unsigned kind = static_cast<unsigned>(target.kind);
    visit(unknown);
    if (!target.descriptor) {
      if (auto found = byKind.find(kind); found != byKind.end())
        visit(found->second);
      return;
    }
    if (auto found = wildcard.find(kind); found != wildcard.end())
      visit(found->second);
    if (auto found = exact.find({kind, *target.descriptor});
        found != exact.end())
      visit(found->second);
  }

private:
  SmallVector<IndexedEffect> all;
  SmallVector<IndexedEffect> unknown;
  std::map<unsigned, SmallVector<IndexedEffect>> byKind;
  std::map<unsigned, SmallVector<IndexedEffect>> wildcard;
  std::map<std::pair<unsigned, uint64_t>, SmallVector<IndexedEffect>> exact;
};

static std::optional<uint64_t> getValueWidth(Type type) {
  if (auto integer = dyn_cast<IntegerType>(type))
    return integer.getWidth();
  if (auto logic = dyn_cast<sim::LogicType>(type))
    return logic.getWidth();
  if (auto reference = dyn_cast<sim::RefType>(type))
    return getValueWidth(reference.getElementType());
  if (auto net = dyn_cast<sim::NetType>(type))
    return getValueWidth(net.getElementType());
  if (auto driver = dyn_cast<sim::DriverType>(type))
    return getValueWidth(driver.getElementType());
  if (isa<sim::EventType>(type))
    return 1;
  return std::nullopt;
}

static ResourceKind getHandleKind(Type type) {
  if (isa<sim::RefType>(type))
    return ResourceKind::Storage;
  if (isa<sim::NetType>(type))
    return ResourceKind::Net;
  if (isa<sim::DriverType>(type))
    return ResourceKind::Net;
  if (isa<sim::EventType>(type))
    return ResourceKind::Event;
  return ResourceKind::Unknown;
}

static Provenance joinProvenance(Provenance lhs, const Provenance &rhs) {
  if (!lhs.valid)
    return rhs;
  if (!rhs.valid)
    return lhs;
  if (lhs.kind != rhs.kind || lhs.descriptor != rhs.descriptor ||
      lhs.formal != rhs.formal) {
    Provenance unknown;
    unknown.valid = true;
    return unknown;
  }
  uint64_t rootWidth = std::max(lhs.rootWidth, rhs.rootWidth);
  auto checkedEnd = [&](uint64_t low, uint64_t width) {
    return low <= rootWidth && width <= rootWidth - low
               ? std::optional<uint64_t>(low + width)
               : std::nullopt;
  };
  std::optional<uint64_t> lhsEnd = checkedEnd(lhs.low, lhs.width);
  std::optional<uint64_t> rhsEnd = checkedEnd(rhs.low, rhs.width);
  if (!lhsEnd || !rhsEnd) {
    lhs.low = 0;
    lhs.width = rootWidth;
    lhs.rootWidth = rootWidth;
    lhs.dynamic = true;
    return lhs;
  }
  uint64_t low = std::min(lhs.low, rhs.low);
  uint64_t end = std::max(*lhsEnd, *rhsEnd);
  lhs.low = low;
  lhs.width = end - low;
  lhs.rootWidth = rootWidth;
  lhs.dynamic |= rhs.dynamic;
  return lhs;
}

static bool effectLess(const Effect &lhs, const Effect &rhs) {
  auto key = [](const Effect &effect) {
    return std::tuple<unsigned, unsigned, uint64_t, unsigned, uint64_t,
                      uint64_t, uint64_t, bool, bool, unsigned, bool>(
        static_cast<unsigned>(effect.kind),
        static_cast<unsigned>(effect.target.kind),
        effect.target.descriptor.value_or(std::numeric_limits<uint64_t>::max()),
        effect.target.formal.value_or(std::numeric_limits<unsigned>::max()),
        effect.target.low, effect.target.width, effect.target.rootWidth,
        effect.target.dynamic, effect.target.valid,
        static_cast<unsigned>(effect.trigger), effect.deferred);
  };
  return key(lhs) < key(rhs);
}

static void normalizeEffects(SmallVectorImpl<Effect> &effects) {
  llvm::sort(effects, effectLess);
  effects.erase(std::unique(effects.begin(), effects.end()), effects.end());
}

static Provenance widenDynamic(Provenance provenance) {
  if (!provenance.valid)
    return provenance;
  provenance.dynamic = true;
  return provenance;
}

static Provenance staticExtract(Provenance provenance, uint64_t low,
                                uint64_t width) {
  if (!provenance.valid)
    return provenance;
  if (provenance.low > provenance.rootWidth ||
      low > provenance.rootWidth - provenance.low ||
      width > provenance.rootWidth - provenance.low - low)
    return widenDynamic(provenance);
  provenance.low += low;
  provenance.width = width;
  return provenance;
}

static void setIfChanged(DenseMap<Value, Provenance> &map, Value value,
                         Provenance provenance, bool &changed) {
  auto found = map.find(value);
  if (found == map.end()) {
    map.try_emplace(value, provenance);
    changed = true;
    return;
  }
  Provenance joined = joinProvenance(found->second, provenance);
  if (!(joined == found->second)) {
    found->second = joined;
    changed = true;
  }
}

static bool allLogicOperandsKnown(Operation *op,
                                  const DenseMap<Value, bool> &known) {
  return llvm::all_of(op->getOperands(), [&](Value operand) {
    return !isa<sim::LogicType>(operand.getType()) || known.lookup(operand);
  });
}

static bool isKnownNonzeroLogic(Value value) {
  auto constant = value.getDefiningOp<sim::SimLogicConstantOp>();
  return constant && constant.getUnknown().isZero() &&
         !constant.getValue().isZero();
}

static bool isKnownLogicResult(Operation *op,
                               const DenseMap<Value, bool> &known) {
  if (auto constant = dyn_cast<sim::SimLogicConstantOp>(op))
    return constant.getUnknown().isZero();
  if (isa<sim::SimLogicFromBitsOp>(op))
    return true;
  if (auto binary = dyn_cast<sim::SimLogicBinaryOp>(op)) {
    if (!allLogicOperandsKnown(op, known))
      return false;
    switch (binary.getKind()) {
    case sim::BinaryKind::UDiv:
    case sim::BinaryKind::SDiv:
    case sim::BinaryKind::UMod:
    case sim::BinaryKind::SMod:
      return isKnownNonzeroLogic(binary.getRhs());
    default:
      return true;
    }
  }
  if (auto compare = dyn_cast<sim::SimLogicCompareOp>(op)) {
    if (compare.getKind() == sim::CompareKind::CaseEq ||
        compare.getKind() == sim::CompareKind::CaseNe)
      return true;
    return allLogicOperandsKnown(op, known);
  }
  if (isa<sim::SimLogicDynExtractOp, sim::SimRefLoadOp, sim::SimNetReadOp,
          sim::SimCallOp>(op))
    return false;
  if (isa<sim::SimLogicResizeOp, sim::SimLogicUnaryOp, sim::SimLogicReductionOp,
          sim::SimLogicLogicalOp, sim::SimLogicShiftOp, sim::SimLogicConcatOp,
          sim::SimLogicReplicateOp, sim::SimLogicExtractOp,
          sim::SimLogicInsertOp>(op))
    return allLogicOperandsKnown(op, known);
  // New logic-producing operations must opt in with a sound transfer rule.
  return false;
}

static void
propagateValueFacts(FunctionInfo &info,
                    const DenseMap<uint64_t, uint64_t> &driverNets) {
  sim::SimFuncOp function = info.function;
  Block &entry = function.getBody().front();
  for (BlockArgument argument : entry.getArguments()) {
    unsigned index = argument.getArgNumber();
    auto capture = function.getArgAttrOfType<sim::CaptureKindAttr>(
        index, "obelisk_sim.capture_kind");
    auto descriptor = function.getArgAttrOfType<IntegerAttr>(
        index, "obelisk_sim.descriptor_id");
    ResourceKind kind = getHandleKind(argument.getType());
    auto width = getValueWidth(argument.getType());
    if (kind != ResourceKind::Unknown && width) {
      Provenance provenance{kind,   std::nullopt, std::nullopt, 0,
                            *width, *width,       false,        true};
      if (capture && capture.getValue() == sim::CaptureKind::Formal)
        provenance.formal = index;
      else if (descriptor) {
        if (!descriptor.getValue().isNegative() &&
            descriptor.getValue().getBitWidth() <= 64)
          provenance.descriptor = descriptor.getValue().getZExtValue();
        else {
          provenance = {};
          provenance.valid = true;
        }
        if (isa<sim::DriverType>(argument.getType())) {
          auto net = provenance.descriptor
                         ? driverNets.find(*provenance.descriptor)
                         : driverNets.end();
          if (net == driverNets.end()) {
            provenance.kind = ResourceKind::Unknown;
            provenance.descriptor.reset();
          } else {
            provenance.descriptor = net->second;
          }
        }
      }
      info.provenance[argument] = provenance;
    }
    if (isa<sim::LogicType>(argument.getType()))
      info.known[argument] = false;
  }

  // Provenance ranges form a finite lattice bounded by each root descriptor.
  // Iterate to a real fixed point rather than publishing a heuristically
  // truncated result.
  while (true) {
    bool changed = false;
    for (Block &block : function.getBody()) {
      if (&block != &entry) {
        for (Block *predecessor : block.getPredecessors()) {
          auto branch =
              dyn_cast<BranchOpInterface>(predecessor->getTerminator());
          if (!branch)
            continue;
          for (unsigned successor = 0;
               successor != predecessor->getTerminator()->getNumSuccessors();
               ++successor) {
            if (predecessor->getTerminator()->getSuccessor(successor) != &block)
              continue;
            auto forwarded =
                branch.getSuccessorOperands(successor).getForwardedOperands();
            unsigned count =
                std::min<unsigned>(forwarded.size(), block.getNumArguments());
            for (unsigned index = 0; index != count; ++index) {
              auto provenance = info.provenance.find(forwarded[index]);
              if (provenance != info.provenance.end())
                setIfChanged(info.provenance, block.getArgument(index),
                             provenance->second, changed);
            }
          }
        }
      }

      for (Operation &operation : block) {
        llvm::TypeSwitch<Operation *>(&operation)
            .Case<sim::SimContextStorageOp>([&](auto op) {
              uint64_t width = *getValueWidth(op.getResult().getType());
              setIfChanged(info.provenance, op.getResult(),
                           {ResourceKind::Storage, op.getId(), std::nullopt, 0,
                            width, width, false, true},
                           changed);
            })
            .Case<sim::SimContextNetOp>([&](auto op) {
              uint64_t width = *getValueWidth(op.getResult().getType());
              setIfChanged(info.provenance, op.getResult(),
                           {ResourceKind::Net, op.getId(), std::nullopt, 0,
                            width, width, false, true},
                           changed);
            })
            .Case<sim::SimContextDriverOp>([&](auto op) {
              uint64_t width = *getValueWidth(op.getResult().getType());
              auto net = driverNets.find(op.getId());
              Provenance provenance{ResourceKind::Unknown,
                                    std::nullopt,
                                    std::nullopt,
                                    0,
                                    width,
                                    width,
                                    false,
                                    true};
              if (net != driverNets.end()) {
                provenance.kind = ResourceKind::Net;
                provenance.descriptor = net->second;
              }
              setIfChanged(info.provenance, op.getResult(), provenance,
                           changed);
            })
            .Case<sim::SimContextEventOp>([&](auto op) {
              setIfChanged(info.provenance, op.getResult(),
                           {ResourceKind::Event, op.getId(), std::nullopt, 0, 1,
                            1, false, true},
                           changed);
            })
            .Case<sim::SimRefAllocOp>([&](auto op) {
              uint64_t width = *getValueWidth(op.getResult().getType());
              setIfChanged(info.provenance, op.getResult(),
                           {ResourceKind::Local, std::nullopt, std::nullopt, 0,
                            width, width, false, true},
                           changed);
            })
            .Case<sim::SimRefExtractOp>([&](auto op) {
              auto input = info.provenance.find(op.getInput());
              if (input != info.provenance.end())
                setIfChanged(
                    info.provenance, op.getResult(),
                    staticExtract(input->second, op.getLowBit(),
                                  *getValueWidth(op.getResult().getType())),
                    changed);
            })
            .Case<sim::SimRefDynExtractOp>([&](auto op) {
              auto input = info.provenance.find(op.getInput());
              if (input != info.provenance.end())
                setIfChanged(info.provenance, op.getResult(),
                             widenDynamic(input->second), changed);
            })
            .Case<sim::SimDriverExtractOp>([&](auto op) {
              auto input = info.provenance.find(op.getInput());
              if (input != info.provenance.end())
                setIfChanged(
                    info.provenance, op.getResult(),
                    staticExtract(input->second, op.getLowBit(),
                                  *getValueWidth(op.getResult().getType())),
                    changed);
            })
            .Case<sim::SimDriverDynExtractOp>([&](auto op) {
              auto input = info.provenance.find(op.getInput());
              if (input != info.provenance.end())
                setIfChanged(info.provenance, op.getResult(),
                             widenDynamic(input->second), changed);
            });
      }
    }
    if (!changed)
      break;
  }

  // Knownness is a separate monotone proof: facts start unproven and only
  // become known after every relevant predecessor and operand is known.
  while (true) {
    bool changed = false;
    for (Block &block : function.getBody()) {
      if (&block != &entry) {
        for (BlockArgument argument : block.getArguments()) {
          if (!isa<sim::LogicType>(argument.getType()) ||
              info.known.lookup(argument))
            continue;
          bool sawIncoming = false;
          bool allIncomingKnown = true;
          for (Block *predecessor : block.getPredecessors()) {
            auto branch =
                dyn_cast<BranchOpInterface>(predecessor->getTerminator());
            if (!branch) {
              allIncomingKnown = false;
              break;
            }
            for (unsigned successor = 0;
                 successor != predecessor->getTerminator()->getNumSuccessors();
                 ++successor) {
              if (predecessor->getTerminator()->getSuccessor(successor) !=
                  &block)
                continue;
              auto forwarded =
                  branch.getSuccessorOperands(successor).getForwardedOperands();
              if (argument.getArgNumber() >= forwarded.size()) {
                allIncomingKnown = false;
              } else {
                Value incoming = forwarded[argument.getArgNumber()];
                // Carrying an argument around a suspension loop preserves,
                // rather than creates, a previously established proof.
                if (incoming != argument && !info.known.lookup(incoming))
                  allIncomingKnown = false;
              }
              sawIncoming = true;
            }
          }
          if (sawIncoming && allIncomingKnown) {
            info.known[argument] = true;
            changed = true;
          }
        }
      }
      for (Operation &operation : block) {
        if (!isKnownLogicResult(&operation, info.known))
          continue;
        for (Value result : operation.getResults())
          if (isa<sim::LogicType>(result.getType()) &&
              !info.known.lookup(result)) {
            info.known[result] = true;
            changed = true;
          }
      }
    }
    if (!changed)
      break;
  }
}

static void
appendEffect(FunctionInfo &info, EffectKind kind, Value handle,
             SmallVectorImpl<Effect> &effects,
             sim::ComputeTriggerKind trigger = sim::ComputeTriggerKind::None,
             bool deferred = false) {
  auto provenance = info.provenance.find(handle);
  if (provenance == info.provenance.end()) {
    Provenance unknown;
    unknown.valid = true;
    effects.push_back({kind, unknown, trigger, deferred});
    return;
  }
  if (provenance->second.kind != ResourceKind::Local)
    effects.push_back({kind, provenance->second, trigger, deferred});
}

static sim::ComputeTriggerKind convert(sim::EdgeKind edge) {
  switch (edge) {
  case sim::EdgeKind::Change:
    return sim::ComputeTriggerKind::Change;
  case sim::EdgeKind::Posedge:
    return sim::ComputeTriggerKind::Posedge;
  case sim::EdgeKind::Negedge:
    return sim::ComputeTriggerKind::Negedge;
  case sim::EdgeKind::Both:
    return sim::ComputeTriggerKind::Both;
  }
  llvm_unreachable("unknown sensitivity edge");
}

static SmallVector<Effect> collectDirectEffects(FunctionInfo &info,
                                                Block *onlyBlock = nullptr) {
  SmallVector<Effect> effects;
  auto visit = [&](Operation *operation) {
    llvm::TypeSwitch<Operation *>(operation)
        .Case<sim::SimRefLoadOp>([&](auto op) {
          appendEffect(info, EffectKind::Read, op.getReference(), effects);
        })
        .Case<sim::SimRefStoreOp>([&](auto op) {
          appendEffect(info, EffectKind::Write, op.getReference(), effects);
        })
        .Case<sim::SimNetReadOp>([&](auto op) {
          appendEffect(info, EffectKind::Read, op.getNet(), effects);
        })
        .Case<sim::SimDriverDriveOp>([&](auto op) {
          appendEffect(info, EffectKind::Drive, op.getDriver(), effects);
        })
        .Case<sim::SimNBAEnqueueOp>([&](auto op) {
          appendEffect(info, EffectKind::NBA, op.getDestination(), effects,
                       sim::ComputeTriggerKind::None,
                       static_cast<bool>(op.getDelay()));
        })
        .Case<sim::SimSuspendChangeOp>([&](auto op) {
          appendEffect(info, EffectKind::Watch, op.getWatched(), effects,
                       sim::ComputeTriggerKind::Change);
        })
        .Case<sim::SimSuspendEdgeOp>([&](auto op) {
          appendEffect(info, EffectKind::Watch, op.getWatched(), effects,
                       convert(op.getEdge()));
        })
        .Case<sim::SimSuspendAnyOp>([&](auto op) {
          for (auto [watched, edge] : llvm::zip(op.getWatched(), op.getEdges()))
            appendEffect(info, EffectKind::Watch, watched, effects,
                         convert(static_cast<sim::EdgeKind>(edge)));
        })
        .Case<sim::SimSuspendEventOp>([&](auto op) {
          appendEffect(info, EffectKind::Watch, op.getEvent(), effects,
                       sim::ComputeTriggerKind::Event);
        })
        .Case<sim::SimEventTriggerOp>([&](auto op) {
          appendEffect(info, EffectKind::Trigger, op.getEvent(), effects,
                       sim::ComputeTriggerKind::None, op.getNonblocking());
        });
  };
  if (onlyBlock)
    for (Operation &operation : *onlyBlock)
      visit(&operation);
  else
    info.function.walk([&](Operation *operation) { visit(operation); });
  normalizeEffects(effects);
  return effects;
}

static Effect substituteEffect(const Effect &effect, sim::SimCallOp call,
                               const FunctionInfo &caller) {
  if (!effect.target.formal)
    return effect;
  unsigned index = *effect.target.formal;
  if (index >= call.getNumOperands()) {
    Provenance unknown;
    unknown.valid = true;
    return {effect.kind, unknown, effect.trigger, effect.deferred};
  }
  auto actual = caller.provenance.find(call.getOperand(index));
  if (actual == caller.provenance.end()) {
    Provenance unknown;
    unknown.valid = true;
    return {effect.kind, unknown, effect.trigger, effect.deferred};
  }
  Provenance target = actual->second;
  if (effect.target.dynamic || target.dynamic)
    target = widenDynamic(target);
  else
    target = staticExtract(target, effect.target.low, effect.target.width);
  return {effect.kind, target, effect.trigger, effect.deferred};
}

static sim::ComputeEffectAttr effectAttribute(MLIRContext *context,
                                              const Effect &effect) {
  Provenance provenance = effect.target;
  if (provenance.kind == ResourceKind::Unknown) {
    provenance = {};
    provenance.valid = true;
  }
  sim::ComputeTargetKind target = sim::ComputeTargetKind::Unknown;
  if (provenance.descriptor)
    target = sim::ComputeTargetKind::Descriptor;
  else if (provenance.formal)
    target = sim::ComputeTargetKind::Formal;
  else if (provenance.kind == ResourceKind::Local)
    target = sim::ComputeTargetKind::Local;
  return sim::ComputeEffectAttr::get(
      context, convert(effect.kind), convert(provenance.kind), target,
      provenance.descriptor.value_or(0), provenance.formal.value_or(0),
      provenance.low, provenance.width, provenance.dynamic, effect.deferred,
      effect.trigger);
}

static ArrayAttr effectArray(OpBuilder &builder, ArrayRef<Effect> effects) {
  SmallVector<Attribute> attributes;
  for (const Effect &effect : effects)
    attributes.push_back(effectAttribute(builder.getContext(), effect));
  return builder.getArrayAttr(attributes);
}

static bool rangesOverlap(const Provenance &lhs, const Provenance &rhs) {
  if (lhs.kind == ResourceKind::Unknown || rhs.kind == ResourceKind::Unknown)
    return true;
  if (lhs.kind != rhs.kind)
    return false;
  if (lhs.descriptor && rhs.descriptor && lhs.descriptor != rhs.descriptor)
    return false;
  // Formal handles may alias each other or any concrete descriptor of their
  // resource class until a static caller/spawn specialization proves more.
  if (!lhs.descriptor || !rhs.descriptor)
    return true;
  if (lhs.dynamic || rhs.dynamic)
    return true;
  if (lhs.low > std::numeric_limits<uint64_t>::max() - lhs.width ||
      rhs.low > std::numeric_limits<uint64_t>::max() - rhs.width)
    return true;
  uint64_t lhsEnd = lhs.low + lhs.width;
  uint64_t rhsEnd = rhs.low + rhs.width;
  return lhs.low < rhsEnd && rhs.low < lhsEnd;
}

static bool isStaticProducer(const Effect &effect) {
  return !effect.deferred && (effect.kind == EffectKind::Write ||
                              effect.kind == EffectKind::Drive ||
                              effect.kind == EffectKind::Trigger);
}

static bool activeEffectsConflict(const Effect &lhs, const Effect &rhs) {
  if (lhs.kind == EffectKind::Watch || rhs.kind == EffectKind::Watch ||
      lhs.kind == EffectKind::NBA || rhs.kind == EffectKind::NBA)
    return false;
  bool lhsWrites = isStaticProducer(lhs);
  bool rhsWrites = isStaticProducer(rhs);
  if (!lhsWrites && !rhsWrites)
    return false;
  return rangesOverlap(lhs.target, rhs.target);
}

static uint64_t operationCost(Operation &operation) {
  if (isa<sim::SimRefLoadOp, sim::SimRefStoreOp, sim::SimNetReadOp,
          sim::SimDriverDriveOp, sim::SimNBAEnqueueOp>(operation))
    return 3;
  if (isa<sim::SimCallOp>(operation))
    return 5;
  if (simlowering::isSuspensionTerminator(&operation))
    return 1;
  return operation.hasTrait<OpTrait::IsTerminator>() ? 0 : 1;
}

static bool fragmentIsTwoState(const FunctionInfo &info, Block &block) {
  for (BlockArgument argument : block.getArguments())
    if (isa<sim::LogicType>(argument.getType()) && !info.known.lookup(argument))
      return false;
  for (Operation &operation : block) {
    DenseSet<Value> continuationOperands;
    if (simlowering::isSuspensionTerminator(&operation)) {
      auto branch = cast<BranchOpInterface>(&operation);
      for (unsigned successor = 0; successor != operation.getNumSuccessors();
           ++successor)
        for (Value value :
             branch.getSuccessorOperands(successor).getForwardedOperands())
          continuationOperands.insert(value);
    }
    for (Value operand : operation.getOperands())
      if (!continuationOperands.contains(operand) &&
          isa<sim::LogicType>(operand.getType()) && !info.known.lookup(operand))
        return false;
    for (Value result : operation.getResults())
      if (isa<sim::LogicType>(result.getType()) && !info.known.lookup(result))
        return false;
  }
  return true;
}

static void normalizeEdges(SmallVectorImpl<Edge> &edges) {
  llvm::sort(edges, [&](const Edge &lhs, const Edge &rhs) {
    auto lhsKey = std::make_tuple(lhs.source, lhs.target, lhs.kind,
                                  lhs.resource.has_value());
    auto rhsKey = std::make_tuple(rhs.source, rhs.target, rhs.kind,
                                  rhs.resource.has_value());
    if (lhsKey != rhsKey)
      return lhsKey < rhsKey;
    return lhs.resource && effectLess(*lhs.resource, *rhs.resource);
  });
  edges.erase(std::unique(edges.begin(), edges.end(),
                          [&](const Edge &lhs, const Edge &rhs) {
                            return lhs.source == rhs.source &&
                                   lhs.target == rhs.target &&
                                   lhs.kind == rhs.kind &&
                                   lhs.resource == rhs.resource;
                          }),
              edges.end());
}

struct SCCResult {
  SmallVector<SmallVector<uint64_t>> groups;
};

template <typename Node>
static SmallVector<SmallVector<Node>> computeStronglyConnectedComponents(
    ArrayRef<Node> nodes, const DenseMap<Node, SmallVector<Node>> &adjacency) {
  DenseMap<Node, unsigned> index, lowlink;
  DenseSet<Node> onStack;
  SmallVector<Node> tarjanStack;
  unsigned nextIndex = 0;
  SmallVector<SmallVector<Node>> components;

  struct Frame {
    Node node;
    size_t nextSuccessor = 0;
    std::optional<Node> parent;
  };
  for (Node root : nodes) {
    if (index.count(root))
      continue;
    index[root] = lowlink[root] = nextIndex++;
    tarjanStack.push_back(root);
    onStack.insert(root);
    SmallVector<Frame> dfs{{root, 0, std::nullopt}};
    while (!dfs.empty()) {
      Frame &frame = dfs.back();
      auto found = adjacency.find(frame.node);
      ArrayRef<Node> successors =
          found == adjacency.end() ? ArrayRef<Node>() : found->second;
      if (frame.nextSuccessor < successors.size()) {
        Node successor = successors[frame.nextSuccessor++];
        if (!index.count(successor)) {
          index[successor] = lowlink[successor] = nextIndex++;
          tarjanStack.push_back(successor);
          onStack.insert(successor);
          dfs.push_back({successor, 0, frame.node});
        } else if (onStack.contains(successor)) {
          lowlink[frame.node] = std::min(lowlink[frame.node], index[successor]);
        }
        continue;
      }

      Node node = frame.node;
      std::optional<Node> parent = frame.parent;
      dfs.pop_back();
      if (parent)
        lowlink[*parent] = std::min(lowlink[*parent], lowlink[node]);
      if (lowlink[node] != index[node])
        continue;
      SmallVector<Node> component;
      while (true) {
        Node member = tarjanStack.pop_back_val();
        onStack.erase(member);
        component.push_back(member);
        if (member == node)
          break;
      }
      llvm::sort(component);
      components.push_back(std::move(component));
    }
  }
  return components;
}

struct ProgramAnalysis {
  DenseMap<uint64_t, uint64_t> driverNets;
  SmallVector<FunctionInfo, 0> functions;
  llvm::StringMap<unsigned> functionIndex;
};

static ProgramAnalysis analyzeProgram(sim::SimDesignOp design) {
  ProgramAnalysis analysis;
  for (sim::SimDriverDeclOp driver :
       design.getBody().front().getOps<sim::SimDriverDeclOp>())
    analysis.driverNets[driver.getId()] = driver.getNetId();

  SmallVector<sim::SimFuncOp> functions(
      design.getBody().front().getOps<sim::SimFuncOp>());
  llvm::sort(functions, [](sim::SimFuncOp lhs, sim::SimFuncOp rhs) {
    return lhs.getSymName() < rhs.getSymName();
  });
  for (sim::SimFuncOp function : functions) {
    analysis.functionIndex[function.getSymName()] = analysis.functions.size();
    analysis.functions.emplace_back(function);
    FunctionInfo &info = analysis.functions.back();
    if (function.getBody().empty()) {
      Provenance unknown;
      unknown.valid = true;
      info.baseEffects = {{EffectKind::Read, unknown},
                          {EffectKind::Write, unknown}};
    } else {
      propagateValueFacts(info, analysis.driverNets);
      info.baseEffects = collectDirectEffects(info);
    }
    info.summary = info.baseEffects;
    function.walk([&](sim::SimCallOp call) { info.calls.push_back(call); });
  }

  SmallVector<SmallVector<unsigned>> callsFrom(analysis.functions.size());
  for (unsigned caller = 0; caller != analysis.functions.size(); ++caller)
    for (sim::SimCallOp call : analysis.functions[caller].calls) {
      auto callee = analysis.functionIndex.find(call.getCallee());
      if (callee != analysis.functionIndex.end())
        callsFrom[caller].push_back(callee->second);
    }

  SmallVector<unsigned> callNodes;
  DenseMap<unsigned, SmallVector<unsigned>> callAdjacency;
  for (unsigned function = 0; function != analysis.functions.size();
       ++function) {
    callNodes.push_back(function);
    llvm::sort(callsFrom[function]);
    callsFrom[function].erase(
        std::unique(callsFrom[function].begin(), callsFrom[function].end()),
        callsFrom[function].end());
    callAdjacency.try_emplace(function, callsFrom[function]);
  }
  SmallVector<SmallVector<unsigned>> callComponents =
      computeStronglyConnectedComponents<unsigned>(callNodes, callAdjacency);
  SmallVector<int> callComponent(analysis.functions.size(), -1);
  for (auto [component, members] : llvm::enumerate(callComponents))
    for (unsigned member : members)
      callComponent[member] = static_cast<int>(component);

  // Publish a deterministic monotone fixed point. Calls within a recursive
  // component and unresolved external calls conservatively touch all state.
  while (true) {
    bool changed = false;
    for (unsigned caller = 0; caller != analysis.functions.size(); ++caller) {
      FunctionInfo &info = analysis.functions[caller];
      SmallVector<Effect> next = info.baseEffects;
      for (sim::SimCallOp call : info.calls) {
        auto callee = analysis.functionIndex.find(call.getCallee());
        if (callee == analysis.functionIndex.end() ||
            callComponent[callee->second] == callComponent[caller]) {
          Provenance unknown;
          unknown.valid = true;
          next.push_back({EffectKind::Read, unknown});
          next.push_back({EffectKind::Write, unknown});
          continue;
        }
        for (const Effect &effect : analysis.functions[callee->second].summary)
          next.push_back(substituteEffect(effect, call, info));
      }
      normalizeEffects(next);
      if (next != info.summary) {
        info.summary = std::move(next);
        changed = true;
      }
    }
    if (!changed)
      break;
  }
  return analysis;
}

static SmallVector<Effect> collectFragmentEffects(ProgramAnalysis &analysis,
                                                  FunctionInfo &info,
                                                  Block &block) {
  SmallVector<Effect> effects = collectDirectEffects(info, &block);
  for (sim::SimCallOp call : block.getOps<sim::SimCallOp>()) {
    auto callee = analysis.functionIndex.find(call.getCallee());
    if (callee == analysis.functionIndex.end()) {
      Provenance unknown;
      unknown.valid = true;
      effects.push_back({EffectKind::Read, unknown});
      effects.push_back({EffectKind::Write, unknown});
      continue;
    }
    for (const Effect &effect : analysis.functions[callee->second].summary)
      effects.push_back(substituteEffect(effect, call, info));
  }
  normalizeEffects(effects);
  return effects;
}

static LogicalResult
verifyRecomputedAnalysisImpl(sim::SimDesignOp design,
                             sim::ComputeGraphAttr graph,
                             simlowering::DescriptorProvenanceMap *provenance) {
  ProgramAnalysis analysis = analyzeProgram(design);
  if (provenance) {
    provenance->clear();
    for (const FunctionInfo &info : analysis.functions)
      for (const auto &[value, fact] : info.provenance)
        if (fact.valid)
          provenance->try_emplace(
              value, simlowering::DescriptorProvenance{
                         convert(fact.kind), fact.descriptor, fact.formal,
                         fact.low, fact.width, fact.rootWidth, fact.dynamic});
  }
  OpBuilder builder(design.getContext());
  DenseMap<Operation *, FunctionInfo *> functionInfo;
  for (FunctionInfo &info : analysis.functions) {
    functionInfo.try_emplace(info.function.getOperation(), &info);
    ArrayAttr actual = info.function.getEffectSummaryAttr();
    if (actual != effectArray(builder, info.summary))
      return info.function.emitOpError(
          "effect summary does not match the executable CFG");
  }
  for (Attribute attribute : graph.getNodes()) {
    auto fragment = dyn_cast<sim::ComputeFragmentAttr>(attribute);
    if (!fragment)
      continue;
    auto function = dyn_cast_or_null<sim::SimFuncOp>(
        SymbolTable::lookupSymbolIn(design, fragment.getFunction()));
    if (!function ||
        fragment.getBlock() >= function.getBody().getBlocks().size())
      return design.emitOpError(
          "cannot recompute analysis for an invalid fragment");
    auto found = functionInfo.find(function.getOperation());
    if (found == functionInfo.end())
      return design.emitOpError("fragment has no function analysis");
    Block &block = *std::next(function.getBody().begin(), fragment.getBlock());
    SmallVector<Effect> effects =
        collectFragmentEffects(analysis, *found->second, block);
    if (fragment.getEffects() != effectArray(builder, effects))
      return design.emitOpError()
             << "fragment " << fragment.getId() << " effects for @"
             << function.getSymName() << " do not match the executable CFG";
    if (fragment.getTwoState() != fragmentIsTwoState(*found->second, block))
      return design.emitOpError()
             << "fragment " << fragment.getId() << " two-state proof for @"
             << function.getSymName() << " does not match the executable CFG";
  }
  return success();
}

static SCCResult computeSCCSchedule(ArrayRef<uint64_t> nodes,
                                    ArrayRef<Edge> edges) {
  DenseMap<uint64_t, SmallVector<uint64_t>> adjacency;
  std::set<uint64_t> nodeSet(nodes.begin(), nodes.end());
  for (const Edge &edge : edges)
    if (edge.kind != sim::ComputeEdgeKind::Resume &&
        nodeSet.count(edge.source) && nodeSet.count(edge.target))
      adjacency[edge.source].push_back(edge.target);
  for (auto &entry : adjacency) {
    llvm::sort(entry.second);
    entry.second.erase(std::unique(entry.second.begin(), entry.second.end()),
                       entry.second.end());
  }

  SmallVector<SmallVector<uint64_t>> components =
      computeStronglyConnectedComponents(nodes, adjacency);

  DenseMap<uint64_t, unsigned> componentOf;
  for (unsigned component = 0; component != components.size(); ++component)
    for (uint64_t node : components[component])
      componentOf[node] = component;
  SmallVector<std::set<unsigned>> successors(components.size());
  SmallVector<unsigned> indegree(components.size());
  for (const Edge &edge : edges) {
    if (edge.kind == sim::ComputeEdgeKind::Resume)
      continue;
    if (!componentOf.count(edge.source) || !componentOf.count(edge.target))
      continue;
    unsigned source = componentOf[edge.source];
    unsigned target = componentOf[edge.target];
    if (source != target && successors[source].insert(target).second)
      ++indegree[target];
  }
  using Ready = std::pair<uint64_t, unsigned>;
  std::priority_queue<Ready, std::vector<Ready>, std::greater<Ready>> ready;
  for (unsigned component = 0; component != components.size(); ++component)
    if (indegree[component] == 0)
      ready.emplace(components[component].front(), component);
  SCCResult result;
  while (!ready.empty()) {
    unsigned component = ready.top().second;
    ready.pop();
    result.groups.push_back(components[component]);
    for (unsigned successor : successors[component])
      if (--indegree[successor] == 0)
        ready.emplace(components[successor].front(), successor);
  }
  return result;
}

static bool hasProceduralControlCycle(ArrayRef<uint64_t> group,
                                      ArrayRef<Edge> edges) {
  DenseSet<uint64_t> members(group.begin(), group.end());
  DenseMap<uint64_t, SmallVector<uint64_t>> successors;
  DenseMap<uint64_t, unsigned> indegree;
  for (uint64_t member : group)
    indegree.try_emplace(member, 0);
  for (const Edge &edge : edges) {
    if (edge.kind != sim::ComputeEdgeKind::ProcessOrder ||
        !members.contains(edge.source) || !members.contains(edge.target))
      continue;
    successors[edge.source].push_back(edge.target);
    ++indegree[edge.target];
  }
  SmallVector<uint64_t> ready;
  for (uint64_t member : group)
    if (indegree[member] == 0)
      ready.push_back(member);
  size_t visited = 0;
  while (!ready.empty()) {
    uint64_t member = ready.pop_back_val();
    ++visited;
    for (uint64_t successor : successors[member])
      if (--indegree[successor] == 0)
        ready.push_back(successor);
  }
  return visited != group.size();
}

class ObeliskSimBuildComputeGraphPass
    : public impl::ObeliskSimBuildComputeGraphPassBase<
          ObeliskSimBuildComputeGraphPass> {
public:
  using Base::Base;
  void runOnOperation() override;
};

void ObeliskSimBuildComputeGraphPass::runOnOperation() {
  sim::SimDesignOp design = getOperation();
  OpBuilder builder(&getContext());

  ProgramAnalysis analysis = analyzeProgram(design);
  auto &functionIndex = analysis.functionIndex;
  auto &infos = analysis.functions;

  for (FunctionInfo &info : infos)
    info.function.setEffectSummaryAttr(effectArray(builder, info.summary));

  SmallVector<Fragment> fragments;
  DenseMap<Block *, uint64_t> fragmentId;
  DenseMap<Operation *, unsigned> infoForFunction;
  uint64_t nextFragment = 0;
  for (unsigned infoIndex = 0; infoIndex != infos.size(); ++infoIndex) {
    FunctionInfo &info = infos[infoIndex];
    infoForFunction[info.function.getOperation()] = infoIndex;
    // Zero-time functions execute in their caller and contribute a substituted
    // summary there; they are not independently schedulable actors.
    if (info.function.getEntryKind() == sim::EntryKind::Function)
      continue;
    uint64_t ordinal = 0;
    for (Block &block : info.function.getBody()) {
      SmallVector<Effect> effects =
          collectFragmentEffects(analysis, info, block);
      uint64_t cost = 0;
      for (Operation &operation : block)
        cost += operationCost(operation);
      fragmentId[&block] = nextFragment;
      fragments.push_back({nextFragment++, info.function, &block, ordinal++,
                           std::move(effects), cost,
                           fragmentIsTwoState(info, block)});
    }
  }

  if (workers == 0 || workers > 65535) {
    design.emitOpError("requested worker count exceeds the lane ID range");
    signalPassFailure();
    return;
  }
  unsigned requestedWorkers = workers;
  std::optional<sim::ComputeVPIMode> vpiMode =
      sim::symbolizeComputeVPIMode(vpi);
  if (!vpiMode) {
    design.emitOpError("VPI mode must be off, read, or full");
    signalPassFailure();
    return;
  }
  SmallVector<uint64_t> laneCost(requestedWorkers);
  SmallVector<unsigned> order(fragments.size());
  for (unsigned index = 0; index != order.size(); ++index)
    order[index] = index;
  llvm::stable_sort(order, [&](unsigned lhs, unsigned rhs) {
    return fragments[lhs].cost > fragments[rhs].cost;
  });
  for (unsigned index : order) {
    unsigned lane =
        std::min_element(laneCost.begin(), laneCost.end()) - laneCost.begin();
    fragments[index].lane = lane;
    laneCost[lane] += fragments[index].cost;
  }

  SmallVector<Edge> edges;
  for (Fragment &fragment : fragments) {
    Operation *terminator = fragment.block->getTerminator();
    for (Block *successor : terminator->getSuccessors())
      edges.push_back({fragment.id, fragmentId.lookup(successor),
                       simlowering::isSuspensionTerminator(terminator)
                           ? sim::ComputeEdgeKind::Resume
                           : sim::ComputeEdgeKind::ProcessOrder,
                       std::nullopt});
    for (sim::SimSpawnOp spawn : fragment.block->getOps<sim::SimSpawnOp>()) {
      auto callee = functionIndex.find(spawn.getCallee());
      if (callee != functionIndex.end() &&
          !infos[callee->second].function.getBody().empty()) {
        Block &entry = infos[callee->second].function.getBody().front();
        edges.push_back({fragment.id, fragmentId.lookup(&entry),
                         sim::ComputeEdgeKind::Spawn, std::nullopt});
      }
    }
  }

  // Static producers directly activate matching sensitivity consumers.
  EffectIndex watchedEffects;
  for (unsigned fragment = 0; fragment != fragments.size(); ++fragment)
    for (const Effect &effect : fragments[fragment].effects)
      if (effect.kind == EffectKind::Watch)
        watchedEffects.add(fragment, effect);
  for (unsigned producer = 0; producer != fragments.size(); ++producer)
    for (const Effect &produced : fragments[producer].effects) {
      if (!isStaticProducer(produced))
        continue;
      watchedEffects.forEachAlias(produced.target, [&](IndexedEffect consumed) {
        if (rangesOverlap(produced.target, consumed.effect->target))
          edges.push_back(
              {fragments[producer].id, fragments[consumed.fragment].id,
               sim::ComputeEdgeKind::Sensitivity, *consumed.effect});
      });
    }

  // Select one stable ordering for conflicting active-region producers. This
  // is a legal SystemVerilog interleaving and makes repeated builds identical.
  EffectIndex activeEffects;
  for (unsigned fragment = 0; fragment != fragments.size(); ++fragment)
    for (const Effect &effect : fragments[fragment].effects)
      if (effect.kind != EffectKind::Watch && effect.kind != EffectKind::NBA)
        activeEffects.add(fragment, effect);
  for (unsigned lhs = 0; lhs != fragments.size(); ++lhs)
    for (const Effect &left : fragments[lhs].effects) {
      if (left.kind == EffectKind::Watch || left.kind == EffectKind::NBA)
        continue;
      activeEffects.forEachAlias(left.target, [&](IndexedEffect right) {
        if (right.fragment <= lhs ||
            fragments[right.fragment].function == fragments[lhs].function ||
            !activeEffectsConflict(left, *right.effect))
          return;
        edges.push_back({fragments[lhs].id, fragments[right.fragment].id,
                         sim::ComputeEdgeKind::Conflict,
                         isStaticProducer(left) ? left : *right.effect});
      });
    }

  auto rootTarget = [](Provenance target) {
    if (target.kind == ResourceKind::Unknown) {
      Provenance unknown;
      unknown.valid = true;
      return unknown;
    }
    if (target.formal && !target.descriptor) {
      Provenance unknown;
      unknown.valid = true;
      return unknown;
    }
    target.low = 0;
    target.width = target.rootWidth;
    target.dynamic = false;
    return target;
  };
  auto targetLess = [](const Provenance &lhs, const Provenance &rhs) {
    return std::tie(lhs.kind, lhs.descriptor, lhs.formal, lhs.low, lhs.width,
                    lhs.rootWidth, lhs.dynamic, lhs.valid) <
           std::tie(rhs.kind, rhs.descriptor, rhs.formal, rhs.low, rhs.width,
                    rhs.rootWidth, rhs.dynamic, rhs.valid);
  };
  auto findTarget = [&](ArrayRef<Provenance> targets,
                        const Provenance &target) -> std::optional<unsigned> {
    auto found = llvm::lower_bound(targets, target, targetLess);
    if (found == targets.end() || !(*found == target))
      return std::nullopt;
    return static_cast<unsigned>(found - targets.begin());
  };

  // Commit all slices of one root descriptor through one ordered journal.
  // This preserves source/site order for overlapping NBA destinations.
  SmallVector<Provenance> nbaTargets;
  SmallVector<std::pair<uint64_t, const Effect *>> nbaStages;
  for (Fragment &fragment : fragments)
    for (const Effect &effect : fragment.effects)
      if (effect.kind == EffectKind::NBA) {
        nbaTargets.push_back(rootTarget(effect.target));
        nbaStages.emplace_back(fragment.id, &effect);
      }
  llvm::sort(nbaTargets, targetLess);
  nbaTargets.erase(std::unique(nbaTargets.begin(), nbaTargets.end()),
                   nbaTargets.end());
  SmallVector<uint64_t> nbaCommitIds;
  for (size_t index = 0; index != nbaTargets.size(); ++index)
    nbaCommitIds.push_back(nextFragment++);
  for (auto [fragment, effect] : nbaStages) {
    unsigned index = *findTarget(nbaTargets, rootTarget(effect->target));
    edges.push_back({fragment, nbaCommitIds[index],
                     sim::ComputeEdgeKind::NBAStage, *effect});
  }
  for (auto [index, target] : llvm::enumerate(nbaTargets)) {
    uint64_t id = nbaCommitIds[index];
    watchedEffects.forEachAlias(target, [&](IndexedEffect consumed) {
      if (rangesOverlap(consumed.effect->target, target))
        edges.push_back({id, fragments[consumed.fragment].id,
                         sim::ComputeEdgeKind::NBAActivate, *consumed.effect});
    });
  }

  // Nonblocking event triggers are deferred into the NBA region rather than
  // acting as immediate active-region producers.
  SmallVector<Provenance> deferredEventTargets;
  SmallVector<std::pair<uint64_t, const Effect *>> deferredEventStages;
  for (const Fragment &fragment : fragments)
    for (const Effect &effect : fragment.effects)
      if (effect.kind == EffectKind::Trigger && effect.deferred) {
        deferredEventTargets.push_back(rootTarget(effect.target));
        deferredEventStages.emplace_back(fragment.id, &effect);
      }
  llvm::sort(deferredEventTargets, targetLess);
  deferredEventTargets.erase(
      std::unique(deferredEventTargets.begin(), deferredEventTargets.end()),
      deferredEventTargets.end());
  SmallVector<uint64_t> eventCommitIds;
  for (size_t index = 0; index != deferredEventTargets.size(); ++index)
    eventCommitIds.push_back(nextFragment++);
  for (auto [fragment, effect] : deferredEventStages) {
    unsigned index =
        *findTarget(deferredEventTargets, rootTarget(effect->target));
    edges.push_back({fragment, eventCommitIds[index],
                     sim::ComputeEdgeKind::DeferredStage, *effect});
  }
  for (auto [index, target] : llvm::enumerate(deferredEventTargets)) {
    uint64_t id = eventCommitIds[index];
    watchedEffects.forEachAlias(target, [&](IndexedEffect consumed) {
      if (rangesOverlap(consumed.effect->target, target))
        edges.push_back({id, fragments[consumed.fragment].id,
                         sim::ComputeEdgeKind::DeferredActivate,
                         *consumed.effect});
    });
  }

  SmallVector<SmallVector<int64_t>> nbaSlots(nbaTargets.size());
  SmallVector<SmallVector<int64_t>> nbaAccumulatorSites(nbaTargets.size());
  SmallVector<SmallVector<int64_t>> nbaStaticJournalSites(nbaTargets.size());
  SmallVector<SmallVector<int64_t>> nbaFrontierSites(nbaTargets.size());
  SmallVector<SmallVector<int64_t>> eventSites(deferredEventTargets.size());
  llvm::SmallDenseSet<Operation *> dynamicallySpawnedFunctions;
  DenseMap<Operation *, unsigned> staticSpawnCounts;
  DenseMap<Operation *, simlowering::ReexecutingBlockSet> reexecutingBlocks;
  for (FunctionInfo &info : infos)
    reexecutingBlocks.try_emplace(
        info.function.getOperation(),
        simlowering::getReexecutingBlocks(info.function));
  auto mayReexecute = [&](sim::SimFuncOp function, Block *block) {
    return reexecutingBlocks.find(function.getOperation())
        ->second.contains(block);
  };
  for (FunctionInfo &info : infos)
    info.function.walk([&](sim::SimSpawnOp spawn) {
      auto callee = functionIndex.find(spawn.getCallee());
      if (callee == functionIndex.end())
        return;
      Operation *target = infos[callee->second].function.getOperation();
      if (info.function.getEntryKind() != sim::EntryKind::RootInitializer ||
          mayReexecute(info.function, spawn->getBlock())) {
        dynamicallySpawnedFunctions.insert(target);
        return;
      }
      if (++staticSpawnCounts[target] > 1)
        dynamicallySpawnedFunctions.insert(target);
    });
  uint64_t timingSite = 0;
  uint64_t nbaSite = 0;
  uint64_t eventSite = 0;
  for (Fragment &fragment : fragments) {
    for (Operation &operation : *fragment.block) {
      if (auto suspension = dyn_cast<BranchOpInterface>(&operation);
          suspension && simlowering::isSuspensionTerminator(&operation)) {
        Block *continuation = operation.getSuccessor(0);
        simlowering::setContinuationSite(
            &operation,
            sim::ContinuationSiteAttr::get(
                &getContext(),
                static_cast<uint32_t>(fragmentId.lookup(continuation))));
      }
      if (auto delay = dyn_cast<sim::SimSuspendDelayOp>(&operation)) {
        delay.setTimingAttr(sim::TimingSiteAttr::get(
            &getContext(), timingSite++,
            simlowering::isConstantTimeValue(delay.getDelay())
                ? sim::ComputeTimingKind::Calendar
                : sim::ComputeTimingKind::DeadlineSlot));
      }
      if (auto nba = dyn_cast<sim::SimNBAEnqueueOp>(&operation)) {
        uint64_t site = nbaSite++;
        auto provenance =
            infos[infoForFunction.lookup(fragment.function.getOperation())]
                .provenance.find(nba.getDestination());
        Provenance destination;
        destination.valid = true;
        if (provenance !=
            infos[infoForFunction.lookup(fragment.function.getOperation())]
                .provenance.end())
          destination = provenance->second;
        Provenance root = rootTarget(destination);
        // A fixed entry is sound only when the site executes at most once over
        // the process lifetime. Repeated immediate assignments to one known
        // root use generated value/unknown/mask and transition accumulators;
        // only delayed or dynamically rooted multiplicity needs the frontier.
        bool fixed =
            fragment.function.getEntryKind() != sim::EntryKind::Function &&
            !dynamicallySpawnedFunctions.contains(
                fragment.function.getOperation()) &&
            !mayReexecute(fragment.function, fragment.block);
        sim::ComputeNBAStorageKind storage =
            fixed ? sim::ComputeNBAStorageKind::FixedSlot
            : !nba.getDelay() && destination.descriptor &&
                    *vpiMode != sim::ComputeVPIMode::Full
                ? sim::ComputeNBAStorageKind::RootAccumulator
                : sim::ComputeNBAStorageKind::DynamicFrontier;
        std::optional<unsigned> targetIndex = findTarget(nbaTargets, root);
        if (!targetIndex) {
          nba.emitOpError("has no generated commit node");
          signalPassFailure();
          return;
        }
        uint32_t commitNode = static_cast<uint32_t>(nbaCommitIds[*targetIndex]);
        switch (storage) {
        case sim::ComputeNBAStorageKind::FixedSlot:
          nbaSlots[*targetIndex].push_back(site);
          break;
        case sim::ComputeNBAStorageKind::RootAccumulator:
          nbaAccumulatorSites[*targetIndex].push_back(site);
          break;
        case sim::ComputeNBAStorageKind::StaticJournal:
          nbaStaticJournalSites[*targetIndex].push_back(site);
          break;
        case sim::ComputeNBAStorageKind::DynamicFrontier:
          nbaFrontierSites[*targetIndex].push_back(site);
          break;
        }
        sim::TimingSiteAttr delayedTiming;
        if (nba.getDelay())
          delayedTiming = sim::TimingSiteAttr::get(
              &getContext(), timingSite++, sim::ComputeTimingKind::DelayedNBA);
        nba.setSiteAttr(sim::NBASiteAttr::get(&getContext(), site, commitNode,
                                              storage, delayedTiming));
      }
      if (auto trigger = dyn_cast<sim::SimEventTriggerOp>(&operation);
          trigger && trigger.getNonblocking()) {
        uint64_t site = eventSite++;
        auto provenance =
            infos[infoForFunction.lookup(fragment.function.getOperation())]
                .provenance.find(trigger.getEvent());
        Provenance destination;
        destination.valid = true;
        if (provenance !=
            infos[infoForFunction.lookup(fragment.function.getOperation())]
                .provenance.end())
          destination = provenance->second;
        Provenance root = rootTarget(destination);
        std::optional<unsigned> targetIndex =
            findTarget(deferredEventTargets, root);
        if (!targetIndex) {
          trigger.emitOpError("has no generated deferred-event commit node");
          signalPassFailure();
          return;
        }
        trigger.setSiteAttr(sim::EventSiteAttr::get(
            &getContext(), site,
            static_cast<uint32_t>(eventCommitIds[*targetIndex])));
        eventSites[*targetIndex].push_back(site);
      }
    }
  }
  normalizeEdges(edges);

  if (nextFragment > std::numeric_limits<uint32_t>::max()) {
    design.emitOpError("compute graph exceeds the 32-bit fragment ABI");
    signalPassFailure();
    return;
  }
  SmallVector<Attribute> nodeAttributes;
  DenseMap<Operation *, SmallVector<int64_t>> functionFragments;
  for (Fragment &fragment : fragments) {
    sim::ComputeFragmentAttr node = sim::ComputeFragmentAttr::get(
        &getContext(), static_cast<uint32_t>(fragment.id),
        FlatSymbolRefAttr::get(&getContext(), fragment.function.getSymName()),
        static_cast<uint32_t>(fragment.ordinal),
        fragment.function.getEntryKind() == sim::EntryKind::Final
            ? sim::ComputeRegionKind::Postponed
            : sim::ComputeRegionKind::Active,
        simlowering::getFragmentActionKind(fragment.block->getTerminator()),
        sim::ComputeTierKind::Native, fragment.cost, fragment.lane,
        fragment.twoState, effectArray(builder, fragment.effects));
    nodeAttributes.push_back(node);
    functionFragments[fragment.function.getOperation()].push_back(fragment.id);
  }
  for (unsigned targetIndex = 0; targetIndex != nbaTargets.size();
       ++targetIndex) {
    const Provenance &target = nbaTargets[targetIndex];
    uint64_t id = nbaCommitIds[targetIndex];
    nodeAttributes.push_back(sim::ComputeNBACommitAttr::get(
        &getContext(), static_cast<uint32_t>(id),
        builder.getDenseI64ArrayAttr(nbaSlots[targetIndex]),
        builder.getDenseI64ArrayAttr(nbaAccumulatorSites[targetIndex]),
        builder.getDenseI64ArrayAttr(nbaStaticJournalSites[targetIndex]),
        builder.getDenseI64ArrayAttr(nbaFrontierSites[targetIndex]),
        effectAttribute(&getContext(), Effect{EffectKind::Write, target})));
  }
  for (unsigned targetIndex = 0; targetIndex != deferredEventTargets.size();
       ++targetIndex) {
    nodeAttributes.push_back(sim::ComputeEventCommitAttr::get(
        &getContext(), static_cast<uint32_t>(eventCommitIds[targetIndex]),
        builder.getDenseI64ArrayAttr(eventSites[targetIndex]),
        effectAttribute(&getContext(),
                        Effect{EffectKind::Trigger,
                               deferredEventTargets[targetIndex],
                               sim::ComputeTriggerKind::None, true})));
  }
  for (FunctionInfo &info : infos) {
    info.function.setFragmentAbiAttr(sim::FragmentABIAttr::get(
        &getContext(), 1,
        builder.getDenseI64ArrayAttr(
            functionFragments[info.function.getOperation()])));
  }

  SmallVector<Attribute> edgeAttributes;
  for (const Edge &edge : edges)
    edgeAttributes.push_back(sim::ComputeEdgeAttr::get(
        &getContext(), static_cast<uint32_t>(edge.source),
        static_cast<uint32_t>(edge.target), edge.kind,
        edge.resource ? effectAttribute(&getContext(), *edge.resource)
                      : sim::ComputeEffectAttr{}));

  SmallVector<uint64_t> activeIds;
  for (Fragment &fragment : fragments)
    if (fragment.function.getEntryKind() != sim::EntryKind::Final)
      activeIds.push_back(fragment.id);
  SCCResult activeSchedule = computeSCCSchedule(activeIds, edges);
  SmallVector<Attribute> regions;
  auto addRegion =
      [&](sim::ComputeRegionKind kind,
          ArrayRef<SmallVector<uint64_t>> groups) -> LogicalResult {
    SmallVector<Attribute> groupAttributes;
    for (ArrayRef<uint64_t> group : groups) {
      SmallVector<int64_t> ids;
      for (uint64_t id : group)
        ids.push_back(id);
      bool cyclic = group.size() > 1;
      if (!cyclic)
        cyclic = llvm::any_of(edges, [&](const Edge &edge) {
          return edge.kind != sim::ComputeEdgeKind::Resume &&
                 edge.source == group.front() && edge.target == group.front();
        });
      sim::ComputeScheduleKind schedule = sim::ComputeScheduleKind::Acyclic;
      SmallVector<Effect> feedbackEffects;
      if (cyclic) {
        if (hasProceduralControlCycle(group, edges)) {
          schedule = sim::ComputeScheduleKind::ControlLoop;
        } else {
          schedule = sim::ComputeScheduleKind::Convergence;
          std::set<uint64_t> members(group.begin(), group.end());
          for (const Edge &edge : edges)
            if (members.count(edge.source) && members.count(edge.target) &&
                edge.kind == sim::ComputeEdgeKind::Sensitivity && edge.resource)
              feedbackEffects.push_back(*edge.resource);
        }
      }
      normalizeEffects(feedbackEffects);
      if (schedule == sim::ComputeScheduleKind::Convergence &&
          feedbackEffects.empty())
        return design.emitOpError(
            "cyclic schedule group has no state feedback to compare");
      groupAttributes.push_back(sim::ComputeGroupAttr::get(
          &getContext(), builder.getDenseI64ArrayAttr(ids), schedule,
          effectArray(builder, feedbackEffects)));
    }
    regions.push_back(sim::ComputeRegionAttr::get(
        &getContext(), kind, builder.getArrayAttr(groupAttributes)));
    return success();
  };
  if (failed(
          addRegion(sim::ComputeRegionKind::Active, activeSchedule.groups))) {
    signalPassFailure();
    return;
  }
  SmallVector<SmallVector<uint64_t>> nbaGroups;
  for (uint64_t id : nbaCommitIds)
    nbaGroups.push_back({id});
  for (uint64_t id : eventCommitIds)
    nbaGroups.push_back({id});
  if (failed(addRegion(sim::ComputeRegionKind::NBA, nbaGroups)) ||
      failed(addRegion(sim::ComputeRegionKind::Observed, {})) ||
      failed(addRegion(sim::ComputeRegionKind::Reactive, {}))) {
    signalPassFailure();
    return;
  }
  SmallVector<SmallVector<uint64_t>> finalGroups;
  for (Fragment &fragment : fragments)
    if (fragment.function.getEntryKind() == sim::EntryKind::Final)
      finalGroups.push_back({fragment.id});
  if (failed(addRegion(sim::ComputeRegionKind::Postponed, finalGroups))) {
    signalPassFailure();
    return;
  }

  sim::ComputeObservabilityKind observability =
      *vpiMode == sim::ComputeVPIMode::Full
          ? sim::ComputeObservabilityKind::ExternallyWritable
      : *vpiMode == sim::ComputeVPIMode::Read
          ? sim::ComputeObservabilityKind::SafePoint
          : sim::ComputeObservabilityKind::Invisible;
  for (sim::SimStorageDeclOp storage :
       design.getBody().front().getOps<sim::SimStorageDeclOp>())
    storage.setObservability(observability);
  for (sim::SimNetDeclOp net :
       design.getBody().front().getOps<sim::SimNetDeclOp>())
    net.setObservability(observability);

  design.setComputeGraphAttr(sim::ComputeGraphAttr::get(
      &getContext(), 1, *vpiMode, requestedWorkers,
      builder.getArrayAttr(nodeAttributes),
      builder.getArrayAttr(edgeAttributes), builder.getArrayAttr(regions)));
}

} // namespace

LogicalResult simlowering::verifyRecomputedComputeAnalysis(
    sim::SimDesignOp design, sim::ComputeGraphAttr graph,
    DescriptorProvenanceMap *provenance) {
  return verifyRecomputedAnalysisImpl(design, graph, provenance);
}

} // namespace obelisk
