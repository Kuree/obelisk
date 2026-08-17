//===- PrepareNetTopology.cpp - Net connection and driver planning -------===//
//
// Freezes static net equivalences and the driver inventory shared by native
// and bytecode execution.
//
//===----------------------------------------------------------------------===//

#include "PrepareNetTopology.h"

#include "Detail.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"

#include <functional>
#include <map>

using namespace mlir;

namespace obelisk::simlowering {

FailureOr<ContinuousDriverMap>
materializeNetTopology(SmallVectorImpl<Operation *> &sourceUnits,
                       ArrayRef<semantic::SVPortConnectionOp> portConnections,
                       const llvm::StringMap<Operation *> &semanticSymbols,
                       const llvm::StringMap<DescriptorInfo> &descriptors,
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
          sim::getPackedWidth(descriptor->second.type);
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
          sim::getPackedWidth(descriptor->second.type);
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
          succeeded(resultType) ? sim::getPackedWidth(*resultType)
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
        succeeded(resultType) ? sim::getPackedWidth(*resultType) : std::nullopt;
    if (failed(normalizedSource) || !width)
      return false;

    if (isa<semantic::SVElementSelectExpressionOp>(expression) &&
        isa<sim::PackedArrayType>(*normalizedSource)) {
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

  bool invalid = false;
  using StaticEdgeKey = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;
  struct StaticEdgeMetadata {
    uint64_t scopeId;
    std::string provenance;
    Location location;
  };
  std::map<StaticEdgeKey, StaticEdgeMetadata> staticEdges;
  auto appendStaticConnections = [&](semantic::SVPortConnectionOp connection,
                                     ArrayRef<NetRun> lhs,
                                     ArrayRef<NetRun> rhs) {
    size_t lhsIndex = 0, rhsIndex = 0;
    uint64_t lhsConsumed = 0, rhsConsumed = 0;
    while (lhsIndex != lhs.size() && rhsIndex != rhs.size()) {
      const NetRun &left = lhs[lhsIndex];
      const NetRun &right = rhs[rhsIndex];
      uint64_t width =
          std::min(left.width - lhsConsumed, right.width - rhsConsumed);
      uint64_t leftOffset = left.offset + lhsConsumed;
      uint64_t rightOffset = right.offset + rhsConsumed;
      if ((left.descriptor.netKind == sim::NetResolutionKind::UWire) !=
          (right.descriptor.netKind == sim::NetResolutionKind::UWire)) {
        emitError(getSemanticLocation(connection))
            << "connected component mixes uwire with resolved wire/tri nets";
        invalid = true;
        return;
      }
      for (uint64_t bit = 0; bit != width; ++bit) {
        StaticEdgeKey edge{left.descriptor.id, leftOffset + bit,
                           right.descriptor.id, rightOffset + bit};
        StaticEdgeKey reverse{right.descriptor.id, rightOffset + bit,
                              left.descriptor.id, leftOffset + bit};
        if (reverse < edge)
          edge = reverse;
        if (std::get<0>(edge) == std::get<2>(edge) &&
            std::get<1>(edge) == std::get<3>(edge))
          continue;
        StaticEdgeMetadata metadata{
            scopes.lookup(connection),
            semantic::stringifySVPortConnectionKind(connection.getProvenance())
                .str(),
            getSemanticLocation(connection)};
        auto [found, inserted] = staticEdges.try_emplace(edge, metadata);
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
    if ((lhsIndex != lhs.size() || rhsIndex != rhs.size()) &&
        connection.getDirection() != semantic::SVArgumentDirection::InOut) {
      emitError(getSemanticLocation(connection))
          << "static net connection has incompatible endpoint widths";
      invalid = true;
    }
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
              sim::getPackedWidth(internalDescriptor->second.type)) {
        lhs.push_back({internalDescriptor->second, 0, *width,
                       internalPath.str(), std::nullopt});
        internalNet = true;
      }
    }
    bool actualNet = flattenNetExpr(actual, rhs);
    if (internalNet && actualNet) {
      appendStaticConnections(connection, lhs, rhs);
      continue;
    }
    if (connection.getDirection() == semantic::SVArgumentDirection::InOut) {
      emitError(getSemanticLocation(connection))
          << "inout port requires a representation-compatible static net "
             "connection";
      invalid = true;
      continue;
    }
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
        builder.getStringAttr(metadata.provenance));
    edge = next;
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
              sim::getPackedWidth(target->second.type);
          if (!width) {
            emitError(getSemanticLocation(unit))
                << "net connection target has no fixed packed width";
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
      std::optional<unsigned> width = sim::getPackedWidth(target->second.type);
      if (!width) {
        emitError(getSemanticLocation(unit))
            << "net initializer target has no fixed packed width";
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

    for (const NetRun &sink : sinks) {
      uint64_t id = nextDriverId++;
      uint64_t scopeId = scopes.lookup(unit);
      DescriptorInfo info{DescriptorInfo::Kind::Driver, id, scopeId,
                          sink.descriptor.type, sink.descriptor.netKind};
      info.rootType = sink.descriptor.type;
      continuousDrivers[unit].push_back(
          {sink.path, info, sink.nodeId, sink.offset, sink.width});
      sim::SimDriverDeclOp::create(
          builder, getSemanticLocation(unit), id, scopeId, sink.descriptor.id,
          sink.descriptor.type, sim::Lifetime::Design,
          builder.getStringAttr(sink.path),
          builder.getStringAttr(connection ? "port connection"
                                : isa<semantic::SVNetSymbolOp>(unit)
                                    ? "net initializer"
                                    : "continuous"),
          builder.getI64IntegerAttr(sink.offset),
          builder.getI64IntegerAttr(sink.width));
    }
  }
  if (invalid)
    return failure();
  return continuousDrivers;
}

} // namespace obelisk::simlowering
