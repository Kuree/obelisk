//===- PrepareNetTopology.cpp - Net connection and driver planning -------===//
//
// Freezes static net equivalences and the driver inventory shared by native
// and bytecode execution.
//
//===----------------------------------------------------------------------===//

#include "PrepareNetTopology.h"

#include "Detail.h"
#include "obelisk/Analysis/NetConnectivityAnalysis.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"

#include <functional>
#include <map>

using namespace mlir;

namespace obelisk::simlowering {

static sim::Strength lowerDriveStrength(semantic::SVDriveStrength strength) {
  switch (strength) {
  case semantic::SVDriveStrength::Supply:
    return sim::Strength::Supply;
  case semantic::SVDriveStrength::Strong:
    return sim::Strength::Strong;
  case semantic::SVDriveStrength::Pull:
    return sim::Strength::Pull;
  case semantic::SVDriveStrength::Weak:
    return sim::Strength::Weak;
  case semantic::SVDriveStrength::HighZ:
    return sim::Strength::HighZ;
  }
  llvm_unreachable("unknown SystemVerilog drive strength");
}

static std::pair<sim::Strength, sim::Strength>
getDriverStrengths(Operation *unit) {
  // IEEE 1800-2017 10.3.4 and 28.3.2 specify strong as the default for
  // continuous assignments and ordinary gates. Pull sources instead default
  // to pull strength (28.10).
  sim::Strength defaultStrength = sim::Strength::Strong;
  bool pullup = false;
  bool pulldown = false;
  if (auto primitive = dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(unit)) {
    auto name = primitive->getAttrOfType<StringAttr>("primitive_name");
    pullup = name && name.getValue() == "pullup";
    pulldown = name && name.getValue() == "pulldown";
    if (pullup || pulldown)
      defaultStrength = sim::Strength::Pull;
  }
  sim::Strength strength0 = defaultStrength;
  sim::Strength strength1 = defaultStrength;
  std::optional<semantic::SVDriveStrength> semanticStrength0;
  std::optional<semantic::SVDriveStrength> semanticStrength1;
  auto readStrengths = [&]<typename OpTy>(OpTy op) {
    semanticStrength0 = op.getDriveStrength0();
    semanticStrength1 = op.getDriveStrength1();
  };
  if (auto assignment = dyn_cast<semantic::SVContinuousAssignSymbolOp>(unit))
    readStrengths(assignment);
  else if (auto primitive =
               dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(unit))
    readStrengths(primitive);
  else if (auto net = dyn_cast<semantic::SVNetSymbolOp>(unit))
    readStrengths(net);
  else if (auto connection = dyn_cast<semantic::SVPortConnectionOp>(unit))
    readStrengths(connection);
  if (semanticStrength0)
    strength0 = lowerDriveStrength(*semanticStrength0);
  if (semanticStrength1)
    strength1 = lowerDriveStrength(*semanticStrength1);
  // IEEE 1800-2017 28.10: only the strength1 of a pullup and strength0 of a
  // pulldown has meaning. Canonicalize the ignored polarity instead of
  // retaining source metadata that must never become observable.
  if (pullup)
    strength0 = sim::Strength::Pull;
  if (pulldown)
    strength1 = sim::Strength::Pull;
  return {strength0, strength1};
}

static bool isConditionalGate(Operation *unit) {
  auto primitive = dyn_cast<semantic::SVPrimitiveInstanceSymbolOp>(unit);
  auto name = primitive ? primitive->getAttrOfType<StringAttr>("primitive_name")
                        : StringAttr{};
  return name && (name.getValue() == "bufif0" || name.getValue() == "bufif1" ||
                  name.getValue() == "notif0" || name.getValue() == "notif1");
}

static Operation *peelClockingOutputSelects(Operation *destination) {
  while (isa<semantic::SVElementSelectExpressionOp,
             semantic::SVRangeSelectExpressionOp>(destination)) {
    SmallVector<Operation *> children = getChildren(destination);
    if (children.empty())
      return nullptr;
    destination = children.front();
  }
  return destination;
}

FailureOr<ContinuousDriverMap>
materializeNetTopology(SmallVectorImpl<Operation *> &sourceUnits,
                       ArrayRef<semantic::SVPortConnectionOp> portConnections,
                       const llvm::StringMap<Operation *> &semanticSymbols,
                       llvm::StringMap<DescriptorInfo> &descriptors,
                       const PreparedScopeDeclarations &scopes,
                       OpBuilder &builder) {
  struct NetRun {
    DescriptorInfo descriptor;
    uint64_t offset;
    uint64_t width;
    std::string path;
    std::optional<uint64_t> nodeId;
  };
  std::function<bool(Operation *, SmallVectorImpl<NetRun> &)> flattenNetExpr;
  flattenNetExpr = [&](Operation *expression,
                       SmallVectorImpl<NetRun> &runs) -> bool {
    if (!expression)
      return false;
    if (auto named = dyn_cast<semantic::SVNamedValueExpressionOp>(expression)) {
      auto descriptor = descriptors.find(named.getReferencedPath());
      if (descriptor == descriptors.end() ||
          descriptor->second.kind != DescriptorInfo::Kind::Net)
        return false;
      std::optional<unsigned> width =
          analysis::getSimulationStorageBitWidth(descriptor->second.type);
      if (!width)
        return false;
      std::optional<uint64_t> nodeId;
      if (auto id = named->getAttrOfType<IntegerAttr>("node_id"))
        nodeId = id.getValue().getZExtValue();
      runs.push_back({descriptor->second, 0, *width,
                      named.getReferencedPath().str(), nodeId});
      return true;
    }
    if (auto hierarchical =
            dyn_cast<semantic::SVHierarchicalValueExpressionOp>(expression)) {
      auto descriptor = descriptors.find(hierarchical.getReferencedPath());
      if (descriptor == descriptors.end() ||
          descriptor->second.kind != DescriptorInfo::Kind::Net)
        return false;
      std::optional<unsigned> width =
          analysis::getSimulationStorageBitWidth(descriptor->second.type);
      if (!width)
        return false;
      std::optional<uint64_t> nodeId;
      if (auto id = hierarchical->getAttrOfType<IntegerAttr>("node_id"))
        nodeId = id.getValue().getZExtValue();
      runs.push_back({descriptor->second, 0, *width,
                      hierarchical.getReferencedPath().str(), nodeId});
      return true;
    }
    if (isa<semantic::SVConcatenationExpressionOp>(expression)) {
      SmallVector<Operation *> children = getChildren(expression);
      for (Operation *child : llvm::reverse(children))
        if (!flattenNetExpr(child, runs))
          return false;
      return true;
    }
    if (auto member =
            dyn_cast<semantic::SVMemberAccessExpressionOp>(expression)) {
      SmallVector<Operation *> children = getChildren(expression);
      if (children.empty())
        return false;
      SmallVector<NetRun> base;
      if (!flattenNetExpr(children.front(), base) || base.size() != 1)
        return false;
      FailureOr<Type> sourceType = getNormalizedSemanticType(children.front());
      auto ordinal = member->getAttrOfType<IntegerAttr>("field_ordinal");
      if (failed(sourceType) || !ordinal || ordinal.getValue().isNegative() ||
          ordinal.getValue().getActiveBits() > 32)
        return false;
      auto subelement = sim::getAggregateProvenanceSubelement(
          *sourceType, static_cast<unsigned>(ordinal.getInt()));
      FailureOr<Type> resultType = getNormalizedSemanticType(expression);
      std::optional<unsigned> resultWidth =
          succeeded(resultType)
              ? analysis::getSimulationStorageBitWidth(*resultType)
              : std::nullopt;
      if (!subelement || !resultWidth ||
          subelement->first > base.front().width ||
          *resultWidth > base.front().width - subelement->first)
        return false;
      runs.push_back({base.front().descriptor,
                      base.front().offset + subelement->first, *resultWidth,
                      base.front().path, base.front().nodeId});
      return true;
    }
    if (!isa<semantic::SVElementSelectExpressionOp,
             semantic::SVRangeSelectExpressionOp>(expression))
      return false;

    SmallVector<Operation *> children = getChildren(expression);
    if (children.size() < 2)
      return false;
    SmallVector<NetRun> base;
    if (!flattenNetExpr(children.front(), base) || base.size() != 1)
      return false;
    auto literalValue = [&](Operation *node) -> std::optional<int64_t> {
      StringAttr spelling = node->getAttrOfType<StringAttr>("constant_value");
      if (!spelling)
        if (auto reference =
                node->getAttrOfType<SymbolRefAttr>("referenced_symbol"))
          if (auto symbol = semanticSymbols.find(reference.getLeafReference());
              symbol != semanticSymbols.end())
            spelling =
                symbol->second->getAttrOfType<StringAttr>("constant_value");
      if (!spelling)
        return std::nullopt;
      FailureOr<ParsedConstant> value =
          parseSVInteger(spelling.getValue(), 64, getSemanticLocation(node));
      if (failed(value) || !value->unknown.isZero() ||
          !value->value.isSignedIntN(64))
        return std::nullopt;
      return value->value.getSExtValue();
    };
    std::optional<int64_t> first = literalValue(children[1]);
    if (!first)
      return false;
    FailureOr<Type> normalizedSource =
        getNormalizedSemanticType(children.front());
    FailureOr<Type> resultType = getNormalizedSemanticType(expression);
    std::optional<unsigned> width =
        succeeded(resultType)
            ? analysis::getSimulationStorageBitWidth(*resultType)
            : std::nullopt;
    if (failed(normalizedSource) || !width)
      return false;

    if (isa<semantic::SVElementSelectExpressionOp>(expression) &&
        isa<sim::PackedArrayType, sim::UnpackedArrayType>(*normalizedSource)) {
      std::optional<unsigned> ordinal =
          sim::getArrayElementOrdinal(*normalizedSource, *first);
      if (!ordinal)
        return false;
      auto subelement =
          sim::getAggregateProvenanceSubelement(*normalizedSource, *ordinal);
      if (!subelement || subelement->first > base.front().width ||
          *width > base.front().width - subelement->first)
        return false;
      runs.push_back({base.front().descriptor,
                      base.front().offset + subelement->first, *width,
                      base.front().path, base.front().nodeId});
      return true;
    }

    auto sourceType =
        children.front()->getAttrOfType<TypeAttr>("semantic_type");
    if (!sourceType)
      return false;
    std::optional<int64_t> right;
    bool descending = true;
    if (auto integral =
            dyn_cast<semantic::IntegralType>(sourceType.getValue())) {
      right = integral.getRight();
      descending = integral.getLeft() >= integral.getRight();
    } else if (auto packed = dyn_cast<semantic::RangedPackedArrayType>(
                   sourceType.getValue())) {
      right = packed.getRight();
      descending = packed.getLeft() >= packed.getRight();
    }
    if (!right)
      return false;
    uint64_t elementSpan = 1;
    if (auto packed = dyn_cast<sim::PackedArrayType>(*normalizedSource)) {
      std::optional<uint64_t> span =
          sim::getProvenanceSpan(packed.getElementType());
      if (!span || *span == 0)
        return false;
      elementSpan = *span;
    }
    auto physical = [&](int64_t index) -> std::optional<uint64_t> {
      llvm::APInt selected(65, static_cast<uint64_t>(index), true);
      llvm::APInt boundary(65, static_cast<uint64_t>(*right), true);
      llvm::APInt offset =
          descending ? selected - boundary : boundary - selected;
      if (offset.isNegative() || offset.getActiveBits() > 64)
        return std::nullopt;
      uint64_t scalarOffset = offset.getZExtValue();
      if (scalarOffset != 0 && elementSpan > UINT64_MAX / scalarOffset)
        return std::nullopt;
      return scalarOffset * elementSpan;
    };
    std::optional<uint64_t> low = physical(*first);
    if (!low)
      return false;
    auto range = dyn_cast<semantic::SVRangeSelectExpressionOp>(expression);
    if (range &&
        range.getSelectionKind() == semantic::SVRangeSelectionKind::Simple) {
      std::optional<int64_t> second = literalValue(children[2]);
      if (!second)
        return false;
      std::optional<uint64_t> other = physical(*second);
      if (!other)
        return false;
      low = std::min(*low, *other);
    } else if (range) {
      bool baseNamesHighBit =
          (descending && range.getSelectionKind() ==
                             semantic::SVRangeSelectionKind::IndexedDown) ||
          (!descending && range.getSelectionKind() ==
                              semantic::SVRangeSelectionKind::IndexedUp);
      if (baseNamesHighBit) {
        if (*width < elementSpan || *low < *width - elementSpan)
          return false;
        *low -= *width - elementSpan;
      }
    }
    if (*low > base.front().width || *width > base.front().width - *low)
      return false;
    runs.push_back({base.front().descriptor, base.front().offset + *low, *width,
                    base.front().path, base.front().nodeId});
    return true;
  };

  std::function<bool(Operation *)> referencesUWireNet;
  referencesUWireNet = [&](Operation *expression) {
    if (!expression)
      return false;
    if (isa<semantic::SVNamedValueExpressionOp,
            semantic::SVHierarchicalValueExpressionOp>(expression)) {
      auto path = expression->getAttrOfType<StringAttr>("referenced_path");
      auto descriptor =
          path ? descriptors.find(path.getValue()) : descriptors.end();
      return descriptor != descriptors.end() &&
             descriptor->second.kind == DescriptorInfo::Kind::Net &&
             descriptor->second.netKind == sim::NetResolutionKind::UWire;
    }
    SmallVector<Operation *> children = getChildren(expression);
    if (isa<semantic::SVAssignmentExpressionOp,
            semantic::SVMemberAccessExpressionOp,
            semantic::SVElementSelectExpressionOp,
            semantic::SVRangeSelectExpressionOp>(expression))
      return !children.empty() && referencesUWireNet(children.front());
    if (isa<semantic::SVConcatenationExpressionOp>(expression))
      return llvm::any_of(children, referencesUWireNet);
    return false;
  };

  bool invalid = false;
  using StaticEdgeKey = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;
  struct StaticEdgeMetadata {
    uint64_t scopeId;
    std::string provenance;
    Location location;
    bool rhsDominates;
  };
  std::map<StaticEdgeKey, StaticEdgeMetadata> staticEdges;
  auto portDominance = [&](sim::NetResolutionKind internal,
                           sim::NetResolutionKind external) {
    // IEEE 1800-2017 Table 23-1. `tri`, `triand`, and `trior` have already
    // been canonicalized to their functionally identical resolution kinds.
    bool internalDominates = false;
    bool warn = false;
    switch (internal) {
    case sim::NetResolutionKind::Wire:
    case sim::NetResolutionKind::Tri:
      break;
    case sim::NetResolutionKind::WAnd:
      internalDominates = external == sim::NetResolutionKind::Wire ||
                          external == sim::NetResolutionKind::Tri;
      warn = external == sim::NetResolutionKind::WOr ||
             external == sim::NetResolutionKind::TriReg ||
             external == sim::NetResolutionKind::Tri0 ||
             external == sim::NetResolutionKind::Tri1 ||
             external == sim::NetResolutionKind::UWire;
      break;
    case sim::NetResolutionKind::WOr:
      internalDominates = external == sim::NetResolutionKind::Wire ||
                          external == sim::NetResolutionKind::Tri;
      warn = external == sim::NetResolutionKind::WAnd ||
             external == sim::NetResolutionKind::TriReg ||
             external == sim::NetResolutionKind::Tri0 ||
             external == sim::NetResolutionKind::Tri1 ||
             external == sim::NetResolutionKind::UWire;
      break;
    case sim::NetResolutionKind::Tri0:
      internalDominates = external == sim::NetResolutionKind::Wire ||
                          external == sim::NetResolutionKind::Tri ||
                          external == sim::NetResolutionKind::TriReg;
      warn = external == sim::NetResolutionKind::WAnd ||
             external == sim::NetResolutionKind::WOr ||
             external == sim::NetResolutionKind::Tri1 ||
             external == sim::NetResolutionKind::UWire;
      break;
    case sim::NetResolutionKind::Tri1:
      internalDominates = external == sim::NetResolutionKind::Wire ||
                          external == sim::NetResolutionKind::Tri ||
                          external == sim::NetResolutionKind::TriReg;
      warn = external == sim::NetResolutionKind::WAnd ||
             external == sim::NetResolutionKind::WOr ||
             external == sim::NetResolutionKind::Tri0 ||
             external == sim::NetResolutionKind::UWire;
      break;
    case sim::NetResolutionKind::UWire:
      internalDominates = external != sim::NetResolutionKind::UWire &&
                          external != sim::NetResolutionKind::Supply0 &&
                          external != sim::NetResolutionKind::Supply1;
      warn = external == sim::NetResolutionKind::WAnd ||
             external == sim::NetResolutionKind::WOr ||
             external == sim::NetResolutionKind::TriReg ||
             external == sim::NetResolutionKind::Tri0 ||
             external == sim::NetResolutionKind::Tri1;
      break;
    case sim::NetResolutionKind::Supply0:
      internalDominates = external != sim::NetResolutionKind::Supply0 &&
                          external != sim::NetResolutionKind::Supply1;
      warn = external == sim::NetResolutionKind::Supply1;
      break;
    case sim::NetResolutionKind::Supply1:
      internalDominates = external != sim::NetResolutionKind::Supply0 &&
                          external != sim::NetResolutionKind::Supply1;
      warn = external == sim::NetResolutionKind::Supply0;
      break;
    case sim::NetResolutionKind::TriReg:
      internalDominates = external == sim::NetResolutionKind::Wire ||
                          external == sim::NetResolutionKind::Tri;
      warn = external == sim::NetResolutionKind::WAnd ||
             external == sim::NetResolutionKind::WOr ||
             external == sim::NetResolutionKind::UWire;
      break;
    }
    return std::pair(!internalDominates, warn);
  };
  auto appendStaticConnections =
      [&](semantic::SVPortConnectionOp connection, ArrayRef<NetRun> lhs,
          ArrayRef<NetRun> rhs) -> std::optional<bool> {
    size_t lhsIndex = 0, rhsIndex = 0;
    uint64_t lhsConsumed = 0, rhsConsumed = 0;
    bool emittedNetTypeWarning = false;
    while (lhsIndex != lhs.size() && rhsIndex != rhs.size()) {
      const NetRun &left = lhs[lhsIndex];
      const NetRun &right = rhs[rhsIndex];
      uint64_t width =
          std::min(left.width - lhsConsumed, right.width - rhsConsumed);
      uint64_t leftOffset = left.offset + lhsConsumed;
      uint64_t rightOffset = right.offset + rhsConsumed;
      for (uint64_t bit = 0; bit != width; ++bit) {
        StaticEdgeKey edge{left.descriptor.id, leftOffset + bit,
                           right.descriptor.id, rightOffset + bit};
        StaticEdgeKey reverse{right.descriptor.id, rightOffset + bit,
                              left.descriptor.id, leftOffset + bit};
        // IEEE 1800-2017 Table 23-1. `left` is the internal endpoint and
        // `right` is the external actual here.
        auto [rhsDominates, warn] =
            portDominance(left.descriptor.netKind, right.descriptor.netKind);
        if (warn && !emittedNetTypeWarning) {
          emitWarning(getSemanticLocation(connection))
              << "dissimilar net types require a port-collapse warning";
          emittedNetTypeWarning = true;
        }
        if (reverse < edge) {
          edge = reverse;
          rhsDominates = !rhsDominates;
        }
        if (std::get<0>(edge) == std::get<2>(edge) &&
            std::get<1>(edge) == std::get<3>(edge))
          continue;
        StaticEdgeMetadata metadata{
            scopes.lookup(connection),
            semantic::stringifySVPortConnectionKind(connection.getProvenance())
                .str(),
            getSemanticLocation(connection), rhsDominates};
        auto [found, inserted] = staticEdges.try_emplace(edge, metadata);
        if (!inserted && found->second.rhsDominates != metadata.rhsDominates) {
          emitError(getSemanticLocation(connection))
              << "static net connection has conflicting dominating sides";
          invalid = true;
          return std::nullopt;
        }
        if (!inserted &&
            std::tie(metadata.scopeId, metadata.provenance) <
                std::tie(found->second.scopeId, found->second.provenance))
          found->second = std::move(metadata);
      }
      lhsConsumed += width;
      rhsConsumed += width;
      if (lhsConsumed == left.width) {
        ++lhsIndex;
        lhsConsumed = 0;
      }
      if (rhsConsumed == right.width) {
        ++rhsIndex;
        rhsConsumed = 0;
      }
    }
    bool fullyMerged = lhsIndex == lhs.size() && rhsIndex == rhs.size();
    if (!fullyMerged &&
        connection.getDirection() != semantic::SVArgumentDirection::InOut) {
      emitError(getSemanticLocation(connection))
          << "static net connection has incompatible endpoint widths";
      invalid = true;
      return std::nullopt;
    }
    return fullyMerged;
  };

  for (semantic::SVPortConnectionOp connection : portConnections) {
    if (connection.getDirection() == semantic::SVArgumentDirection::Ref)
      continue;
    StringRef internalPath = connection.getInternalPath().value_or(StringRef{});
    auto internalDescriptor = descriptors.find(internalPath);
    if (internalDescriptor == descriptors.end()) {
      if (connection.getInterfaceInstanceSymbol() ||
          isa<semantic::UntypedType>(connection.getFormalType()))
        continue;
      emitError(getSemanticLocation(connection))
          << "port internal endpoint has no flattened descriptor";
      invalid = true;
      continue;
    }
    Operation *actual = getPortActualLValue(connection);
    if (!actual) {
      if (connection.getUnconnectedDriveValue())
        sourceUnits.push_back(connection);
      continue;
    }

    SmallVector<NetRun> lhs, rhs;
    Operation *internalExpression =
        getSingleRegionRoot(connection.getInternal());
    bool internalNet = false;
    if (internalExpression) {
      internalNet = flattenNetExpr(internalExpression, lhs);
    } else if (internalDescriptor->second.kind == DescriptorInfo::Kind::Net) {
      if (std::optional<unsigned> width =
              analysis::getSimulationStorageBitWidth(
                  internalDescriptor->second.type)) {
        lhs.push_back({internalDescriptor->second, 0, *width,
                       internalPath.str(), std::nullopt});
        internalNet = true;
      }
    }
    bool actualNet = flattenNetExpr(actual, rhs);
    bool hasUWireSide =
        (internalDescriptor->second.kind == DescriptorInfo::Kind::Net &&
         internalDescriptor->second.netKind == sim::NetResolutionKind::UWire) ||
        referencesUWireNet(actual);
    if (internalNet && actualNet) {
      std::optional<bool> fullyMerged =
          appendStaticConnections(connection, lhs, rhs);
      if (fullyMerged && !*fullyMerged && hasUWireSide)
        emitWarning(getSemanticLocation(connection))
            << "uwire port connection was not fully merged into a single "
               "simulated net";
      continue;
    }
    if (connection.getDirection() == semantic::SVArgumentDirection::InOut) {
      emitError(getSemanticLocation(connection))
          << "inout port requires a representation-compatible static net "
             "connection";
      invalid = true;
      continue;
    }
    if (hasUWireSide)
      emitWarning(getSemanticLocation(connection))
          << "uwire port connection was not fully merged into a single "
             "simulated net";
    sourceUnits.push_back(connection);
  }
  if (invalid)
    return failure();

  uint64_t nextConnectionId = 0;
  for (auto edge = staticEdges.begin(); edge != staticEdges.end();) {
    auto [lhsNet, lhsOffset, rhsNet, rhsOffset] = edge->first;
    const StaticEdgeMetadata metadata = edge->second;
    uint64_t width = 1;
    int direction = 0;
    auto next = std::next(edge);
    while (next != staticEdges.end()) {
      auto [nextLhsNet, nextLhsOffset, nextRhsNet, nextRhsOffset] = next->first;
      if (next->second.scopeId != metadata.scopeId ||
          next->second.provenance != metadata.provenance ||
          next->second.rhsDominates != metadata.rhsDominates ||
          nextLhsNet != lhsNet || nextRhsNet != rhsNet ||
          nextLhsOffset != lhsOffset + width)
        break;
      int candidateDirection = 0;
      if (nextRhsOffset == rhsOffset + width)
        candidateDirection = 1;
      else if (rhsOffset >= width && nextRhsOffset == rhsOffset - width)
        candidateDirection = -1;
      if (candidateDirection == 0 ||
          (direction != 0 && candidateDirection != direction))
        break;
      direction = candidateDirection;
      ++width;
      ++next;
    }
    sim::SimNetConnectDeclOp::create(
        builder, metadata.location, nextConnectionId++, metadata.scopeId,
        lhsNet, lhsOffset, rhsNet, rhsOffset, width, direction < 0,
        builder.getStringAttr(metadata.provenance),
        builder.getBoolAttr(metadata.rhsDominates));
    edge = next;
  }

  // Preserve the external/internal direction long enough to apply the LRM
  // 23.3.3.7 dominating net's delay to every declaration participating in
  // the collapsed simulated net. Mixed vector components use one encoded
  // triple per bit; an all--1 triple denotes a bit with no net delay.
  auto design =
      dyn_cast<sim::SimDesignOp>(builder.getInsertionBlock()->getParentOp());
  if (!design)
    return failure();
  analysis::NetConnectivityAnalysis connectivity(design);
  DenseMap<uint64_t, sim::SimNetDeclOp> netDeclarations;
  for (sim::SimNetDeclOp net : design.getBody().getOps<sim::SimNetDeclOp>())
    netDeclarations[net.getId()] = net;
  using DelayTriple = std::array<int64_t, 3>;
  auto declaredDelay = [&](analysis::NetBit bit) -> std::optional<DelayTriple> {
    auto found = netDeclarations.find(bit.net);
    if (found == netDeclarations.end())
      return std::nullopt;
    auto delays =
        found->second->getAttrOfType<DenseI64ArrayAttr>("propagation_delays");
    if (!delays)
      return std::nullopt;
    ArrayRef<int64_t> values = delays.asArrayRef();
    size_t index = values.size() == 3 ? 0 : size_t{bit.offset} * 3;
    if (index + 3 > values.size() || values[index] == -1)
      return std::nullopt;
    return DelayTriple{values[index], values[index + 1], values[index + 2]};
  };
  DenseMap<uint64_t, SmallVector<std::optional<DelayTriple>>> effectiveDelays;
  for (auto entry : netDeclarations) {
    uint64_t netId = entry.first;
    sim::SimNetDeclOp declaration = entry.second;
    std::optional<unsigned> width = sim::getPackedWidth(declaration.getType());
    if (!width)
      continue;
    effectiveDelays[netId].resize(*width);
    for (uint64_t bit = 0; bit != *width; ++bit) {
      ArrayRef<analysis::NetBit> component =
          connectivity.getComponent({netId, bit});
      bool componentHasDelay =
          llvm::any_of(component, [&](analysis::NetBit member) {
            return declaredDelay(member).has_value();
          });
      analysis::NetDominance dominance =
          connectivity.getDominance({netId, bit});
      ArrayRef<analysis::NetBit> dominatingBits =
          connectivity.getDominatingBits({netId, bit});
      if (componentHasDelay &&
          (dominance.kind == analysis::NetDominanceKind::Incomplete ||
           dominatingBits.empty())) {
        declaration.emitError(
            dominance.kind == analysis::NetDominanceKind::Incomplete
                ? "delayed collapsed net is missing port-dominance direction"
                : "delayed collapsed net has ambiguous port dominance");
        return failure();
      }
      if (componentHasDelay &&
          llvm::any_of(dominatingBits, [&](analysis::NetBit member) {
            return declaredDelay(member) !=
                   declaredDelay(dominatingBits.front());
          })) {
        declaration.emitError(
            "delayed collapsed net has ambiguous dominating delays");
        return failure();
      }
      analysis::NetBit effective =
          dominatingBits.empty() ? dominance.bit : dominatingBits.front();
      effectiveDelays[netId][bit] = declaredDelay(effective);
    }
  }
  for (auto entry : netDeclarations) {
    uint64_t netId = entry.first;
    sim::SimNetDeclOp declaration = entry.second;
    auto foundDelays = effectiveDelays.find(netId);
    if (foundDelays == effectiveDelays.end())
      continue;
    ArrayRef<std::optional<DelayTriple>> delays = foundDelays->second;
    bool anyDelay = llvm::any_of(
        delays, [](const auto &delay) { return delay.has_value(); });
    if (!anyDelay) {
      declaration->removeAttr("propagation_delays");
      continue;
    }
    bool uniform = llvm::all_of(
        delays, [&](const auto &delay) { return delay == delays.front(); });
    SmallVector<int64_t> encoded;
    if (uniform) {
      llvm::append_range(encoded, *delays.front());
    } else {
      for (const auto &delay : delays) {
        if (delay)
          llvm::append_range(encoded, *delay);
        else
          encoded.append(3, -1);
      }
    }
    declaration->setAttr("propagation_delays",
                         builder.getDenseI64ArrayAttr(encoded));
  }
  for (auto &[path, descriptor] : descriptors)
    if (descriptor.kind == DescriptorInfo::Kind::Net) {
      auto found = effectiveDelays.find(descriptor.id);
      if (found != effectiveDelays.end())
        descriptor.delayedNet = llvm::any_of(
            found->second, [](const auto &delay) { return delay.has_value(); });
    }

  ContinuousDriverMap continuousDrivers;
  uint64_t nextDriverId = 0;
  std::function<void(Operation *, SmallVectorImpl<NetRun> &)> collectDriverRuns;
  collectDriverRuns = [&](Operation *expression,
                          SmallVectorImpl<NetRun> &runs) {
    if (!expression)
      return;
    if (isa<semantic::SVConcatenationExpressionOp>(expression)) {
      for (Operation *child : getChildren(expression))
        collectDriverRuns(child, runs);
      return;
    }
    SmallVector<NetRun> exact;
    if (flattenNetExpr(expression, exact)) {
      llvm::append_range(runs, exact);
      return;
    }
    for (Operation *child : getChildren(expression)) {
      size_t before = runs.size();
      collectDriverRuns(child, runs);
      if (runs.size() != before)
        return;
    }
  };
  for (Operation *unit : sourceUnits) {
    bool continuous =
        isa<semantic::SVContinuousAssignSymbolOp,
            semantic::SVPrimitiveInstanceSymbolOp, semantic::SVNetSymbolOp>(
            unit);
    auto connection = dyn_cast<semantic::SVPortConnectionOp>(unit);
    if (!continuous && !connection)
      continue;
    auto [strength0, strength1] = getDriverStrengths(unit);
    SmallVector<NetRun> sinks;
    if (connection &&
        connection.getDirection() == semantic::SVArgumentDirection::In) {
      Operation *internal = getSingleRegionRoot(connection.getInternal());
      if (internal) {
        collectDriverRuns(internal, sinks);
      } else {
        StringRef path = connection.getInternalPath().value_or(StringRef{});
        auto target = descriptors.find(path);
        if (target == descriptors.end()) {
          emitError(getSemanticLocation(unit))
              << "connection target is not a flattened design object: " << path;
          invalid = true;
          continue;
        }
        if (target->second.kind == DescriptorInfo::Kind::Net) {
          std::optional<unsigned> width =
              analysis::getSimulationStorageBitWidth(target->second.type);
          if (!width) {
            emitError(getSemanticLocation(unit))
                << "net connection target has no fixed storage width";
            invalid = true;
            continue;
          }
          sinks.push_back(
              {target->second, 0, *width, path.str(), std::nullopt});
        }
      }
    } else if (auto net = dyn_cast<semantic::SVNetSymbolOp>(unit)) {
      auto target = descriptors.find(getHierarchyName(net));
      if (target == descriptors.end() ||
          target->second.kind != DescriptorInfo::Kind::Net) {
        emitError(getSemanticLocation(unit))
            << "net initializer target has no flattened net descriptor";
        invalid = true;
        continue;
      }
      std::optional<unsigned> width =
          analysis::getSimulationStorageBitWidth(target->second.type);
      if (!width) {
        emitError(getSemanticLocation(unit))
            << "net initializer target has no fixed storage width";
        invalid = true;
        continue;
      }
      sinks.push_back({target->second, 0, *width, getHierarchyName(net).str(),
                       std::nullopt});
    } else if (isa<semantic::SVPrimitiveInstanceSymbolOp>(unit)) {
      for (Operation *root : getChildren(unit)) {
        auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(root);
        if (!assignment)
          break;
        SmallVector<Operation *> children = getChildren(assignment);
        if (children.empty()) {
          emitError(getSemanticLocation(unit))
              << "primitive output has no resolved assignment lvalue";
          invalid = true;
          break;
        }
        collectDriverRuns(children.front(), sinks);
      }
      if (invalid)
        continue;
    } else {
      Operation *assignmentRoot = nullptr;
      if (connection) {
        assignmentRoot = getSingleRegionRoot(connection.getActual());
      } else {
        SmallVector<Operation *> roots = getChildren(unit);
        if (!roots.empty())
          assignmentRoot = roots.front();
      }
      auto assignment =
          dyn_cast_or_null<semantic::SVAssignmentExpressionOp>(assignmentRoot);
      SmallVector<Operation *> children =
          assignment ? getChildren(assignment) : SmallVector<Operation *>{};
      if (!assignment || children.size() != 2) {
        emitError(getSemanticLocation(unit))
            << "connection source has no resolved assignment lvalue";
        invalid = true;
        continue;
      }
      collectDriverRuns(children.front(), sinks);
    }

    bool conditionalGate = isConditionalGate(unit);
    for (const NetRun &sink : sinks) {
      unsigned bankCount = conditionalGate ? 2 : 1;
      uint64_t strengthGroup = nextDriverId;
      for (unsigned bank = 0; bank != bankCount; ++bank) {
        uint64_t id = nextDriverId++;
        uint64_t scopeId = scopes.lookup(unit);
        DescriptorInfo info{DescriptorInfo::Kind::Driver, id, scopeId,
                            sink.descriptor.type, sink.descriptor.netKind};
        info.rootType = sink.descriptor.type;
        info.delayedNet = sink.descriptor.delayedNet;
        continuousDrivers[unit].push_back(
            {sink.path, info, sink.nodeId, sink.offset, sink.width,
             conditionalGate ? std::optional<unsigned>(bank) : std::nullopt});
        auto driver = sim::SimDriverDeclOp::create(
            builder, getSemanticLocation(unit), id, scopeId, sink.descriptor.id,
            sink.descriptor.type, sim::Lifetime::Design,
            builder.getStringAttr(sink.path),
            builder.getStringAttr(connection ? "port connection"
                                  : isa<semantic::SVNetSymbolOp>(unit)
                                      ? "net initializer"
                                      : "continuous"),
            builder.getI64IntegerAttr(sink.offset),
            builder.getI64IntegerAttr(sink.width));
        sim::Strength driverStrength0 =
            conditionalGate && bank == 1 ? sim::Strength::HighZ : strength0;
        sim::Strength driverStrength1 =
            conditionalGate && bank == 0 ? sim::Strength::HighZ : strength1;
        driver->setAttr(
            "strength0",
            sim::StrengthAttr::get(builder.getContext(), driverStrength0));
        driver->setAttr(
            "strength1",
            sim::StrengthAttr::get(builder.getContext(), driverStrength1));
        if (conditionalGate) {
          driver->setAttr("obelisk_sim.strength_group",
                          builder.getI64IntegerAttr(strengthGroup));
          driver->setAttr("obelisk_sim.strength_bank",
                          builder.getI32IntegerAttr(bank));
        }
      }
    }
  }

  // A clocking output that names a net contributes one procedural driver per
  // clocking-block output. Every syntactic drive site captures that shared
  // driver by its own lvalue node ID, while ordinary reads remain bound to the
  // resolved net itself.
  llvm::DenseSet<Operation *> sourceUnitSet(sourceUnits.begin(),
                                            sourceUnits.end());
  llvm::StringMap<DescriptorInfo> staticClockingDrivers;
  for (Operation *unit : sourceUnits) {
    unit->walk<WalkOrder::PreOrder>([&](Operation *nested) -> WalkResult {
      if (nested != unit && sourceUnitSet.contains(nested))
        return WalkResult::skip();
      auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(nested);
      if (!assignment)
        return WalkResult::advance();
      SmallVector<Operation *> children = getChildren(assignment);
      size_t destinationIndex = assignment.getHasTimingControl() ? 1 : 0;
      if (children.size() <= destinationIndex)
        return WalkResult::advance();
      Operation *destination =
          peelClockingOutputSelects(children[destinationIndex]);
      if (!destination)
        return WalkResult::advance();
      auto direction =
          destination->getAttrOfType<semantic::SVArgumentDirectionAttr>(
              clockingAccessDirectionAttrName);
      if (!destination->hasAttr(clockingVariableAttrName) || !direction ||
          direction.getValue() == semantic::SVArgumentDirection::In)
        return WalkResult::advance();
      auto path =
          destination->getAttrOfType<StringAttr>(clockingSourcePathAttrName);
      auto clockingPath =
          destination->getAttrOfType<StringAttr>("referenced_path");
      auto node = destination->getAttrOfType<IntegerAttr>("node_id");
      if (!clockingPath || !node)
        return WalkResult::advance();
      SmallVector<std::pair<StringAttr, uint64_t>> netLeaves;
      if (path) {
        netLeaves.emplace_back(path, node.getValue().getZExtValue());
      } else {
        auto reference =
            destination->getAttrOfType<SymbolRefAttr>("referenced_symbol");
        semantic::SVClockVarSymbolOp declaration;
        if (reference)
          destination->getParentOfType<ModuleOp>().walk(
              [&](semantic::SVClockVarSymbolOp candidate) {
                if (!declaration && candidate.getSymName() ==
                                        reference.getLeafReference().getValue())
                  declaration = candidate;
              });
        SmallVector<Operation *> declarationChildren =
            declaration ? getChildren(declaration) : SmallVector<Operation *>{};
        if (declarationChildren.size() == 1)
          declarationChildren.front()->walk([&](Operation *expression) {
            auto leafPath =
                expression->getAttrOfType<StringAttr>("referenced_path");
            auto leafNode = expression->getAttrOfType<IntegerAttr>("node_id");
            if (!leafPath || !leafNode)
              return;
            auto descriptor = descriptors.find(leafPath.getValue());
            if (descriptor != descriptors.end() &&
                descriptor->second.kind == DescriptorInfo::Kind::Net)
              netLeaves.emplace_back(leafPath,
                                     leafNode.getValue().getZExtValue());
          });
      }
      for (auto [leafPath, leafNode] : netLeaves) {
        auto sink = descriptors.find(leafPath.getValue());
        if (sink == descriptors.end() ||
            sink->second.kind != DescriptorInfo::Kind::Net)
          continue;
        std::optional<unsigned> width =
            analysis::getSimulationStorageBitWidth(sink->second.type);
        if (!width) {
          emitError(getSemanticLocation(destination))
              << "clocking output net has no fixed storage width";
          invalid = true;
          return WalkResult::interrupt();
        }
        std::string driverKey =
            (Twine(clockingPath.getValue()) + "\n" + leafPath.getValue()).str();
        auto shared = staticClockingDrivers.find(driverKey);
        if (shared == staticClockingDrivers.end()) {
          uint64_t id = nextDriverId++;
          DescriptorInfo info{DescriptorInfo::Kind::Driver, id,
                              sink->second.scopeId, sink->second.type,
                              sink->second.netKind};
          info.rootType = sink->second.type;
          info.delayedNet = sink->second.delayedNet;
          shared = staticClockingDrivers.try_emplace(driverKey, info).first;
          sim::SimDriverDeclOp::create(
              builder, getSemanticLocation(destination), id,
              sink->second.scopeId, sink->second.id, sink->second.type,
              sim::Lifetime::Design, leafPath,
              builder.getStringAttr("clocking output"),
              builder.getI64IntegerAttr(0), builder.getI64IntegerAttr(*width));
        } else if (shared->second.type != sink->second.type ||
                   shared->second.scopeId != sink->second.scopeId) {
          emitError(getSemanticLocation(destination))
              << "clocking output net resolves to inconsistent targets";
          invalid = true;
          return WalkResult::interrupt();
        }
        continuousDrivers[unit].push_back(
            {leafPath.getValue().str(), shared->second, leafNode, 0, *width});
      }
      return WalkResult::advance();
    });
    if (invalid)
      break;
  }
  if (invalid)
    return failure();

  // A virtual-interface clocking output can select any elaborated interface
  // instance at runtime. Give each syntactic output site one driver in every
  // matching instance; unit lowering selects the driver by the handle's scope.
  DenseMap<uint64_t, StringAttr> interfaceScopes;
  llvm::StringMap<SmallVector<sim::SimNetDeclOp>> interfaceNets;
  Block *inventory = builder.getInsertionBlock();
  for (Operation &operation : *inventory)
    if (auto scope = dyn_cast<sim::SimScopeDeclOp>(operation))
      if (StringAttr identity = scope.getInterfaceTypeAttr())
        interfaceScopes[scope.getId()] = identity;
  for (Operation &operation : *inventory) {
    auto net = dyn_cast<sim::SimNetDeclOp>(operation);
    if (!net)
      continue;
    auto scope = interfaceScopes.find(net.getScopeId());
    StringAttr member =
        net->getAttrOfType<StringAttr>("obelisk_sim.virtual_interface_member");
    if (scope == interfaceScopes.end() || !member)
      continue;
    std::string key =
        (Twine(scope->second.getValue()) + "\n" + member.getValue()).str();
    interfaceNets[key].push_back(net);
  }

  struct VirtualClockingDriver {
    DescriptorInfo descriptor;
    uint64_t netID;
  };
  llvm::StringMap<VirtualClockingDriver> virtualClockingDrivers;
  for (Operation *unit : sourceUnits) {
    unit->walk<WalkOrder::PreOrder>([&](Operation *nested) -> WalkResult {
      if (nested != unit && sourceUnitSet.contains(nested))
        return WalkResult::skip();
      auto assignment = dyn_cast<semantic::SVAssignmentExpressionOp>(nested);
      if (!assignment)
        return WalkResult::advance();
      SmallVector<Operation *> children = getChildren(assignment);
      size_t destinationIndex = assignment.getHasTimingControl() ? 1 : 0;
      if (children.size() <= destinationIndex)
        return WalkResult::advance();
      auto destination = dyn_cast_or_null<semantic::SVMemberAccessExpressionOp>(
          peelClockingOutputSelects(children[destinationIndex]));
      if (!destination || !destination->hasAttr("virtual_interface_clocking"))
        return WalkResult::advance();
      auto direction =
          destination->getAttrOfType<semantic::SVArgumentDirectionAttr>(
              "virtual_interface_access_direction");
      auto member = destination->getAttrOfType<StringAttr>("member_name");
      if (auto source = destination->getAttrOfType<StringAttr>(
              "virtual_interface_clocking_signal_member"))
        member = source;
      auto node = destination->getAttrOfType<IntegerAttr>("node_id");
      auto clockingPath =
          destination->getAttrOfType<StringAttr>("referenced_path");
      SmallVector<Operation *> receiver = getChildren(destination);
      size_t expectedReceiverChildren =
          destination->hasAttr("virtual_interface_clock_event_has_iff") ? 3 : 1;
      FailureOr<Type> receiverType =
          receiver.size() == expectedReceiverChildren
              ? getNormalizedSemanticType(receiver.front())
              : FailureOr<Type>(failure());
      auto interfaceType =
          succeeded(receiverType)
              ? dyn_cast<sim::VirtualInterfaceType>(*receiverType)
              : sim::VirtualInterfaceType{};
      if (!direction ||
          direction.getValue() == semantic::SVArgumentDirection::In ||
          !member || !node || !clockingPath || !interfaceType)
        return WalkResult::advance();
      std::string key = (Twine(interfaceType.getInterfaceName().getValue()) +
                         "\n" + member.getValue())
                            .str();
      auto targets = interfaceNets.find(key);
      if (targets == interfaceNets.end())
        return WalkResult::advance();
      for (sim::SimNetDeclOp net : targets->second) {
        std::optional<unsigned> width =
            analysis::getSimulationStorageBitWidth(net.getType());
        if (!width) {
          emitError(getSemanticLocation(destination))
              << "virtual clocking output net has no fixed storage width";
          invalid = true;
          return WalkResult::interrupt();
        }
        StringRef path = net.getHierarchicalName().value_or(StringRef{});
        std::string driverKey =
            (Twine(clockingPath.getValue()) + "\n" + Twine(net.getScopeId()))
                .str();
        auto shared = virtualClockingDrivers.find(driverKey);
        if (shared == virtualClockingDrivers.end()) {
          uint64_t id = nextDriverId++;
          DescriptorInfo info{DescriptorInfo::Kind::Driver, id,
                              net.getScopeId(), net.getType(),
                              net.getResolutionKind()};
          info.rootType = net.getType();
          info.delayedNet = static_cast<bool>(net.getPropagationDelays());
          shared = virtualClockingDrivers
                       .try_emplace(driverKey,
                                    VirtualClockingDriver{info, net.getId()})
                       .first;
          std::string hierarchy =
              (Twine(path) + ".$clocking_output." + clockingPath.getValue())
                  .str();
          auto driver = sim::SimDriverDeclOp::create(
              builder, getSemanticLocation(destination), id, net.getScopeId(),
              net.getId(), net.getType(), sim::Lifetime::Design,
              builder.getStringAttr(hierarchy),
              builder.getStringAttr("virtual clocking output"),
              builder.getI64IntegerAttr(0), builder.getI64IntegerAttr(*width));
          driver->setAttr("obelisk_sim.virtual_interface_member", member);
          driver->setAttr("obelisk_sim.virtual_interface_clocking_output",
                          clockingPath);
        } else if (shared->second.netID != net.getId() ||
                   shared->second.descriptor.type != net.getType()) {
          emitError(getSemanticLocation(destination))
              << "virtual clocking output resolves to inconsistent targets";
          invalid = true;
          return WalkResult::interrupt();
        }
        continuousDrivers[destination].push_back(
            {path.str(), shared->second.descriptor, std::nullopt, 0, *width});
      }
      return WalkResult::advance();
    });
    if (invalid)
      break;
  }
  if (invalid)
    return failure();
  return continuousDrivers;
}

} // namespace obelisk::simlowering
