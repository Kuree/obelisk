//===- VPIValueLayout.cpp - Exact reflected element descriptor checks -----===//
#include "RuntimeInternal.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <vector>

namespace {
struct ManagedSlot {
  uint64_t offset = 0;
  uint32_t mask = 0;
  uint32_t builtin = 0;
  bool conditional = false;
};

ManagedSlot managedLeaf(uint32_t kind) {
  switch (kind) {
  case OBELISK_RT_DESIGN_SEMANTIC_CLASS:
    return {0, OBELISK_RT_MANAGED_ROOT_KIND_CLASS};
  case OBELISK_RT_DESIGN_SEMANTIC_STRING:
    return {0, OBELISK_RT_MANAGED_ROOT_KIND_STRING};
  case OBELISK_RT_DESIGN_SEMANTIC_MAILBOX:
  case OBELISK_RT_DESIGN_SEMANTIC_SEMAPHORE:
    return {0,
            OBELISK_RT_MANAGED_ROOT_KIND_CLASS |
                OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER,
            kind == OBELISK_RT_DESIGN_SEMANTIC_MAILBOX
                ? OBELISK_RT_MANAGED_SLOT_MAILBOX
                : OBELISK_RT_MANAGED_SLOT_SEMAPHORE};
  case OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY:
  case OBELISK_RT_DESIGN_SEMANTIC_QUEUE:
  case OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY:
  case OBELISK_RT_DESIGN_SEMANTIC_UNTYPED:
    return {0, OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER};
  default:
    return {};
  }
}

bool aggregateKind(uint32_t kind) {
  return kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_STRUCT ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_PACKED_UNION ||
         kind == OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION;
}

uint32_t traceKind(const ManagedSlot &slot) {
  if (slot.conditional)
    return OBELISK_RT_MANAGED_SLOT_CANDIDATE | slot.mask;
  if (slot.builtin)
    return slot.builtin;
  switch (slot.mask) {
  case OBELISK_RT_MANAGED_ROOT_KIND_CLASS:
    return OBELISK_RT_MANAGED_SLOT_CLASS;
  case OBELISK_RT_MANAGED_ROOT_KIND_STRING:
    return OBELISK_RT_MANAGED_SLOT_STRING;
  case OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER:
    return OBELISK_RT_MANAGED_SLOT_CONTAINER;
  default:
    return OBELISK_RT_MANAGED_SLOT_INVALID;
  }
}

using TraceSlots = std::vector<std::pair<uint64_t, uint32_t>>;

obelisk_rt_status expectedTrace(const obelisk_rt_context *context,
                                VPIValueLayoutOwner owner,
                                obelisk_rt_design_cursor_v1 root,
                                uint64_t valueSize, TraceSlots &out) {
  // Compute which semantic subgraphs contain managed words before expanding
  // fixed dimensions. Large arrays of ordinary bits require no per-element
  // work. Container elements live in another allocation and are not descended.
  struct Node {
    obelisk_rt_design_semantic_type_info_v1 info{};
    std::vector<obelisk_rt_design_cursor_v1> children;
    bool hasManaged = false;
  };
  std::unordered_map<uint64_t, Node> nodes;
  std::vector<std::pair<obelisk_rt_design_cursor_v1, bool>> pending{
      {root, false}};
  while (!pending.empty()) {
    auto [cursor, complete] = pending.back();
    pending.pop_back();
    if (complete) {
      auto &node = nodes.at(cursor.offset);
      for (auto child : node.children)
        node.hasManaged |= nodes.at(child.offset).hasManaged;
      continue;
    }
    if (nodes.count(cursor.offset))
      continue;
    Node node;
    auto status = obelisk_rt_cached_design_semantic_type_info(context, cursor,
                                                              &node.info);
    if (status != OBELISK_RT_OK)
      return status;
    node.hasManaged = managedLeaf(node.info.kind).mask != 0;
    if (!node.hasManaged && aggregateKind(node.info.kind)) {
      for (uint32_t i = 0; i != node.info.edge_count; ++i) {
        obelisk_rt_design_semantic_type_edge_v1 edge{};
        status = obelisk_rt_cached_design_semantic_type_edge(context, cursor, i,
                                                             &edge);
        if (status != OBELISK_RT_OK)
          return status;
        node.children.push_back(edge.child);
      }
    }
    pending.push_back({cursor, true});
    for (auto child : node.children)
      pending.push_back({child, false});
    nodes.emplace(cursor.offset, std::move(node));
  }
  struct Value {
    obelisk_rt_design_cursor_v1 semantic;
    uint64_t offset = 0;
    bool conditional = false;
  };
  std::vector<Value> values{{root}};
  std::vector<ManagedSlot> slots;
  while (!values.empty()) {
    Value value = values.back();
    values.pop_back();
    const auto &node = nodes.at(value.semantic.offset);
    if (!node.hasManaged)
      continue;
    ManagedSlot slot = managedLeaf(node.info.kind);
    if (slot.mask) {
      if (value.offset % 8 || value.offset / 8 > valueSize ||
          sizeof(obelisk_rt_managed_word_v1) > valueSize - value.offset / 8)
        return OBELISK_RT_INVALID_DESIGN;
      slot.offset = value.offset / 8;
      slot.conditional = value.conditional;
      slots.push_back(slot);
      continue;
    }
    obelisk_rt_design_cursor_v1 physical{};
    auto status = obelisk_rt_cached_vpi_value_element_type(
        context, owner, value.semantic, &physical);
    if (status != OBELISK_RT_OK)
      return status;
    obelisk_rt_design_type_info_v1 type{};
    status = obelisk_rt_cached_design_type_info(context, physical, &type);
    if (status != OBELISK_RT_OK)
      return status;
    const bool overlapping =
        type.kind == OBELISK_RT_DESIGN_TYPE_UNION &&
        ((type.flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0 ||
         (type.flags & OBELISK_RT_DESIGN_TYPE_TAGGED) == 0);
    if (type.kind == OBELISK_RT_DESIGN_TYPE_ARRAY) {
      if (node.children.size() != 1)
        return OBELISK_RT_INVALID_DESIGN;
      uint64_t distance = type.range_left >= type.range_right
                              ? static_cast<uint64_t>(type.range_left) -
                                    static_cast<uint64_t>(type.range_right)
                              : static_cast<uint64_t>(type.range_right) -
                                    static_cast<uint64_t>(type.range_left);
      if (distance == UINT64_MAX || type.bit_width % (distance + 1))
        return OBELISK_RT_INVALID_DESIGN;
      uint64_t count = distance + 1, stride = type.bit_width / count;
      for (uint64_t i = 0; i != count; ++i) {
        if (i && stride > (UINT64_MAX - value.offset) / i)
          return OBELISK_RT_INVALID_DESIGN;
        values.push_back(
            {node.children[0], value.offset + i * stride, value.conditional});
      }
    } else {
      for (uint64_t i = 0; i != node.children.size(); ++i) {
        obelisk_rt_design_cursor_v1 field{};
        obelisk_rt_design_type_info_v1 info{};
        if (obelisk_rt_cached_design_type_child(context, physical, i, &field) !=
                OBELISK_RT_OK ||
            obelisk_rt_cached_design_type_info(context, field, &info) !=
                OBELISK_RT_OK ||
            info.packed_offset > UINT64_MAX - value.offset)
          return OBELISK_RT_INVALID_DESIGN;
        values.push_back({node.children[i], value.offset + info.packed_offset,
                          value.conditional || overlapping});
      }
    }
  }
  std::sort(slots.begin(), slots.end(),
            [](const auto &a, const auto &b) { return a.offset < b.offset; });
  for (size_t i = 0; i != slots.size();) {
    ManagedSlot slot = slots[i++];
    while (i != slots.size() && slots[i].offset == slot.offset) {
      slot.mask |= slots[i].mask;
      slot.conditional |= slots[i].conditional;
      if (slot.builtin != slots[i].builtin)
        slot.builtin = 0;
      ++i;
    }
    uint32_t kind = traceKind(slot);
    if (!kind)
      return OBELISK_RT_INVALID_DESIGN;
    out.emplace_back(slot.offset, kind);
  }
  return OBELISK_RT_OK;
}

bool matchingTrace(const obelisk_rt_trace_layout_v1 *root,
                   const TraceSlots &expected) {
  if (!root)
    return expected.empty();
  // element_type_validate already proves this immutable layout is acyclic.
  // Empty embedded graphs can be repeated without expanding their instances.
  std::unordered_map<const obelisk_rt_trace_layout_v1 *, bool> hasHandles;
  std::vector<std::pair<const obelisk_rt_trace_layout_v1 *, bool>> pending{
      {root, false}};
  while (!pending.empty()) {
    auto [layout, complete] = pending.back();
    pending.pop_back();
    if (complete) {
      bool handles = false;
      for (uint64_t i = 0; i != layout->entry_count; ++i) {
        const auto &entry = layout->entries[i];
        handles |= entry.kind != OBELISK_RT_TRACE_EMBEDDED ||
                   hasHandles.at(entry.child_layout);
      }
      hasHandles[layout] = handles;
      continue;
    }
    if (hasHandles.count(layout))
      continue;
    hasHandles.emplace(layout, false);
    pending.push_back({layout, true});
    for (uint64_t i = 0; i != layout->entry_count; ++i)
      if (layout->entries[i].kind == OBELISK_RT_TRACE_EMBEDDED)
        pending.push_back({layout->entries[i].child_layout, false});
  }
  struct Frame {
    const obelisk_rt_trace_layout_v1 *layout;
    uint64_t offset = 0, entry = 0, repeat = 0;
  };
  std::vector<Frame> stack{{root}};
  TraceSlots actual;
  while (!stack.empty()) {
    auto &frame = stack.back();
    if (!hasHandles.at(frame.layout) ||
        frame.entry == frame.layout->entry_count) {
      stack.pop_back();
      continue;
    }
    const auto &entry = frame.layout->entries[frame.entry];
    if (entry.kind == OBELISK_RT_TRACE_EMBEDDED &&
        !hasHandles.at(entry.child_layout)) {
      ++frame.entry;
      continue;
    }
    const uint64_t offset =
        frame.offset + entry.offset + frame.repeat * entry.stride;
    if (++frame.repeat == entry.count) {
      ++frame.entry;
      frame.repeat = 0;
    }
    if (entry.kind == OBELISK_RT_TRACE_EMBEDDED)
      stack.push_back({entry.child_layout, offset});
    else {
      if (entry.kind != OBELISK_RT_TRACE_STRONG ||
          actual.size() == expected.size())
        return false;
      actual.emplace_back(offset, entry.slot_kind);
    }
  }
  std::sort(actual.begin(), actual.end());
  return actual == expected;
}
} // namespace

obelisk_rt_status
obelisk_rt_vpi_element_matches(const obelisk_rt_context *context,
                               VPIValueLayoutOwner owner,
                               obelisk_rt_design_cursor_v1 semantic,
                               const obelisk_rt_element_type_v1 *element) {
  if (!context || !element)
    return OBELISK_RT_INVALID_ARGUMENT;
  const auto validation = obelisk_rt_v1_element_type_validate(element);
  if (validation != OBELISK_RT_OK)
    return validation == OBELISK_RT_INVALID_DESIGN ? OBELISK_RT_INVALID_ARGUMENT
                                                   : validation;
  obelisk_rt_design_cursor_v1 physical{};
  auto status = obelisk_rt_cached_vpi_value_element_type(context, owner,
                                                         semantic, &physical);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk_rt_design_semantic_type_info_v1 source{};
  obelisk_rt_design_type_info_v1 storage{};
  if (obelisk_rt_cached_design_semantic_type_info(context, semantic, &source) !=
          OBELISK_RT_OK ||
      obelisk_rt_cached_design_type_info(context, physical, &storage) !=
          OBELISK_RT_OK)
    return OBELISK_RT_INVALID_DESIGN;
  if (!storage.bit_width || storage.bit_width > UINT64_MAX - 7)
    return OBELISK_RT_INVALID_DESIGN;
  uint64_t size = (storage.bit_width + 7) / 8;
  uint64_t width = storage.bit_width;
  const bool fourState =
      (storage.flags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
  uint32_t kind =
      fourState ? OBELISK_RT_ELEMENT_LOGIC : OBELISK_RT_ELEMENT_BITS;
  switch (source.kind) {
  case OBELISK_RT_DESIGN_SEMANTIC_SHORT_REAL:
  case OBELISK_RT_DESIGN_SEMANTIC_REAL:
  case OBELISK_RT_DESIGN_SEMANTIC_REALTIME:
    kind = OBELISK_RT_ELEMENT_REAL;
    break;
  case OBELISK_RT_DESIGN_SEMANTIC_CLASS:
    kind = OBELISK_RT_ELEMENT_CLASS_HANDLE;
    width = 0;
    break;
  case OBELISK_RT_DESIGN_SEMANTIC_STRING:
    kind = OBELISK_RT_ELEMENT_STRING;
    width = 0;
    break;
  case OBELISK_RT_DESIGN_SEMANTIC_EVENT:
    kind = OBELISK_RT_ELEMENT_EVENT;
    width = 0;
    break;
  case OBELISK_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY:
  case OBELISK_RT_DESIGN_SEMANTIC_QUEUE:
  case OBELISK_RT_DESIGN_SEMANTIC_ASSOC_ARRAY:
  case OBELISK_RT_DESIGN_SEMANTIC_MAILBOX:
  case OBELISK_RT_DESIGN_SEMANTIC_SEMAPHORE:
  case OBELISK_RT_DESIGN_SEMANTIC_UNTYPED:
    kind = OBELISK_RT_ELEMENT_CONTAINER_HANDLE;
    width = 0;
    break;
  case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY:
  case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT:
  case OBELISK_RT_DESIGN_SEMANTIC_UNPACKED_UNION:
    kind = OBELISK_RT_ELEMENT_AGGREGATE;
    width = size * 8;
    break;
  default:
    break;
  }
  // Signedness belongs to source semantics; native integer storage is
  // signless. Alignment affects allocation, not the copied element shape,
  // and the runtime descriptor validator has already checked its validity.
  if (element->kind != kind || element->value_size != size ||
      element->bit_width != width ||
      ((element->flags & OBELISK_RT_ELEMENT_FOUR_STATE) != 0) != fourState)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    TraceSlots expected;
    status = expectedTrace(context, owner, semantic, size, expected);
    if (status != OBELISK_RT_OK)
      return status;
    return matchingTrace(element->trace, expected)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_ARGUMENT;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_OUT_OF_RESOURCES; }
}
