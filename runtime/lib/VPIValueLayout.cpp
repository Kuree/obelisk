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
  case TALOS_RT_DESIGN_SEMANTIC_CLASS:
    return {0, TALOS_RT_MANAGED_ROOT_KIND_CLASS};
  case TALOS_RT_DESIGN_SEMANTIC_STRING:
    return {0, TALOS_RT_MANAGED_ROOT_KIND_STRING};
  case TALOS_RT_DESIGN_SEMANTIC_MAILBOX:
  case TALOS_RT_DESIGN_SEMANTIC_SEMAPHORE:
    return {0,
            TALOS_RT_MANAGED_ROOT_KIND_CLASS |
                TALOS_RT_MANAGED_ROOT_KIND_CONTAINER,
            static_cast<uint32_t>(kind == TALOS_RT_DESIGN_SEMANTIC_MAILBOX
                                      ? TALOS_RT_MANAGED_SLOT_MAILBOX
                                      : TALOS_RT_MANAGED_SLOT_SEMAPHORE)};
  case TALOS_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY:
  case TALOS_RT_DESIGN_SEMANTIC_QUEUE:
  case TALOS_RT_DESIGN_SEMANTIC_ASSOC_ARRAY:
  case TALOS_RT_DESIGN_SEMANTIC_UNTYPED:
    return {0, TALOS_RT_MANAGED_ROOT_KIND_CONTAINER};
  default:
    return {};
  }
}

bool aggregateKind(uint32_t kind) {
  return kind == TALOS_RT_DESIGN_SEMANTIC_PACKED_ARRAY ||
         kind == TALOS_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY ||
         kind == TALOS_RT_DESIGN_SEMANTIC_PACKED_STRUCT ||
         kind == TALOS_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT ||
         kind == TALOS_RT_DESIGN_SEMANTIC_PACKED_UNION ||
         kind == TALOS_RT_DESIGN_SEMANTIC_UNPACKED_UNION;
}

uint32_t traceKind(const ManagedSlot &slot) {
  if (slot.conditional)
    return TALOS_RT_MANAGED_SLOT_CANDIDATE | slot.mask;
  if (slot.builtin)
    return slot.builtin;
  switch (slot.mask) {
  case TALOS_RT_MANAGED_ROOT_KIND_CLASS:
    return TALOS_RT_MANAGED_SLOT_CLASS;
  case TALOS_RT_MANAGED_ROOT_KIND_STRING:
    return TALOS_RT_MANAGED_SLOT_STRING;
  case TALOS_RT_MANAGED_ROOT_KIND_CONTAINER:
    return TALOS_RT_MANAGED_SLOT_CONTAINER;
  default:
    return TALOS_RT_MANAGED_SLOT_INVALID;
  }
}

using TraceSlots = std::vector<std::pair<uint64_t, uint32_t>>;

talos_rt_status expectedTrace(const talos_rt_context *context,
                                VPIValueLayoutOwner owner,
                                talos_rt_design_cursor_v1 root,
                                uint64_t valueSize, TraceSlots &out) {
  // Compute which semantic subgraphs contain managed words before expanding
  // fixed dimensions. Large arrays of ordinary bits require no per-element
  // work. Container elements live in another allocation and are not descended.
  struct Node {
    talos_rt_design_semantic_type_info_v1 info{};
    std::vector<talos_rt_design_cursor_v1> children;
    bool hasManaged = false;
  };
  std::unordered_map<uint64_t, Node> nodes;
  std::vector<std::pair<talos_rt_design_cursor_v1, bool>> pending{
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
    auto status = talos_rt_cached_design_semantic_type_info(context, cursor,
                                                              &node.info);
    if (status != TALOS_RT_OK)
      return status;
    node.hasManaged = managedLeaf(node.info.kind).mask != 0;
    if (!node.hasManaged && aggregateKind(node.info.kind)) {
      for (uint32_t i = 0; i != node.info.edge_count; ++i) {
        talos_rt_design_semantic_type_edge_v1 edge{};
        status = talos_rt_cached_design_semantic_type_edge(context, cursor, i,
                                                             &edge);
        if (status != TALOS_RT_OK)
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
    talos_rt_design_cursor_v1 semantic;
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
          sizeof(talos_rt_managed_word_v1) > valueSize - value.offset / 8)
        return TALOS_RT_INVALID_DESIGN;
      slot.offset = value.offset / 8;
      slot.conditional = value.conditional;
      slots.push_back(slot);
      continue;
    }
    talos_rt_design_cursor_v1 physical{};
    auto status = talos_rt_cached_vpi_value_element_type(
        context, owner, value.semantic, &physical);
    if (status != TALOS_RT_OK)
      return status;
    talos_rt_design_type_info_v1 type{};
    status = talos_rt_cached_design_type_info(context, physical, &type);
    if (status != TALOS_RT_OK)
      return status;
    const bool overlapping =
        type.kind == TALOS_RT_DESIGN_TYPE_UNION &&
        ((type.flags & TALOS_RT_DESIGN_TYPE_PACKED) != 0 ||
         (type.flags & TALOS_RT_DESIGN_TYPE_TAGGED) == 0);
    if (type.kind == TALOS_RT_DESIGN_TYPE_ARRAY) {
      if (node.children.size() != 1)
        return TALOS_RT_INVALID_DESIGN;
      uint64_t distance = type.range_left >= type.range_right
                              ? static_cast<uint64_t>(type.range_left) -
                                    static_cast<uint64_t>(type.range_right)
                              : static_cast<uint64_t>(type.range_right) -
                                    static_cast<uint64_t>(type.range_left);
      if (distance == UINT64_MAX || type.bit_width % (distance + 1))
        return TALOS_RT_INVALID_DESIGN;
      uint64_t count = distance + 1, stride = type.bit_width / count;
      for (uint64_t i = 0; i != count; ++i) {
        if (i && stride > (UINT64_MAX - value.offset) / i)
          return TALOS_RT_INVALID_DESIGN;
        values.push_back(
            {node.children[0], value.offset + i * stride, value.conditional});
      }
    } else {
      for (uint64_t i = 0; i != node.children.size(); ++i) {
        talos_rt_design_cursor_v1 field{};
        talos_rt_design_type_info_v1 info{};
        if (talos_rt_cached_design_type_child(context, physical, i, &field) !=
                TALOS_RT_OK ||
            talos_rt_cached_design_type_info(context, field, &info) !=
                TALOS_RT_OK ||
            info.packed_offset > UINT64_MAX - value.offset)
          return TALOS_RT_INVALID_DESIGN;
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
      return TALOS_RT_INVALID_DESIGN;
    out.emplace_back(slot.offset, kind);
  }
  return TALOS_RT_OK;
}

bool matchingTrace(const talos_rt_trace_layout_v1 *root,
                   const TraceSlots &expected) {
  if (!root)
    return expected.empty();
  // element_type_validate already proves this immutable layout is acyclic.
  // Empty embedded graphs can be repeated without expanding their instances.
  std::unordered_map<const talos_rt_trace_layout_v1 *, bool> hasHandles;
  std::vector<std::pair<const talos_rt_trace_layout_v1 *, bool>> pending{
      {root, false}};
  while (!pending.empty()) {
    auto [layout, complete] = pending.back();
    pending.pop_back();
    if (complete) {
      bool handles = false;
      for (uint64_t i = 0; i != layout->entry_count; ++i) {
        const auto &entry = layout->entries[i];
        handles |= entry.kind != TALOS_RT_TRACE_EMBEDDED ||
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
      if (layout->entries[i].kind == TALOS_RT_TRACE_EMBEDDED)
        pending.push_back({layout->entries[i].child_layout, false});
  }
  struct Frame {
    const talos_rt_trace_layout_v1 *layout;
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
    if (entry.kind == TALOS_RT_TRACE_EMBEDDED &&
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
    if (entry.kind == TALOS_RT_TRACE_EMBEDDED)
      stack.push_back({entry.child_layout, offset});
    else {
      if (entry.kind != TALOS_RT_TRACE_STRONG ||
          actual.size() == expected.size())
        return false;
      actual.emplace_back(offset, entry.slot_kind);
    }
  }
  std::sort(actual.begin(), actual.end());
  return actual == expected;
}
} // namespace

talos_rt_status
talos_rt_vpi_element_matches(const talos_rt_context *context,
                               VPIValueLayoutOwner owner,
                               talos_rt_design_cursor_v1 semantic,
                               const talos_rt_element_type_v1 *element) {
  if (!context || !element)
    return TALOS_RT_INVALID_ARGUMENT;
  const auto validation = talos_rt_v1_element_type_validate(element);
  if (validation != TALOS_RT_OK)
    return validation == TALOS_RT_INVALID_DESIGN ? TALOS_RT_INVALID_ARGUMENT
                                                   : validation;
  talos_rt_design_cursor_v1 physical{};
  auto status = talos_rt_cached_vpi_value_element_type(context, owner,
                                                         semantic, &physical);
  if (status != TALOS_RT_OK)
    return status;
  talos_rt_design_semantic_type_info_v1 source{};
  talos_rt_design_type_info_v1 storage{};
  if (talos_rt_cached_design_semantic_type_info(context, semantic, &source) !=
          TALOS_RT_OK ||
      talos_rt_cached_design_type_info(context, physical, &storage) !=
          TALOS_RT_OK)
    return TALOS_RT_INVALID_DESIGN;
  if (!storage.bit_width || storage.bit_width > UINT64_MAX - 7)
    return TALOS_RT_INVALID_DESIGN;
  uint64_t size = (storage.bit_width + 7) / 8;
  uint64_t width = storage.bit_width;
  const bool fourState =
      (storage.flags & TALOS_RT_DESIGN_TYPE_FOUR_STATE) != 0;
  uint32_t kind =
      fourState ? TALOS_RT_ELEMENT_LOGIC : TALOS_RT_ELEMENT_BITS;
  switch (source.kind) {
  case TALOS_RT_DESIGN_SEMANTIC_SHORT_REAL:
  case TALOS_RT_DESIGN_SEMANTIC_REAL:
  case TALOS_RT_DESIGN_SEMANTIC_REALTIME:
    kind = TALOS_RT_ELEMENT_REAL;
    break;
  case TALOS_RT_DESIGN_SEMANTIC_CLASS:
    kind = TALOS_RT_ELEMENT_CLASS_HANDLE;
    width = 0;
    break;
  case TALOS_RT_DESIGN_SEMANTIC_STRING:
    kind = TALOS_RT_ELEMENT_STRING;
    width = 0;
    break;
  case TALOS_RT_DESIGN_SEMANTIC_EVENT:
    kind = TALOS_RT_ELEMENT_EVENT;
    width = 0;
    break;
  case TALOS_RT_DESIGN_SEMANTIC_DYNAMIC_ARRAY:
  case TALOS_RT_DESIGN_SEMANTIC_QUEUE:
  case TALOS_RT_DESIGN_SEMANTIC_ASSOC_ARRAY:
  case TALOS_RT_DESIGN_SEMANTIC_MAILBOX:
  case TALOS_RT_DESIGN_SEMANTIC_SEMAPHORE:
  case TALOS_RT_DESIGN_SEMANTIC_UNTYPED:
    kind = TALOS_RT_ELEMENT_CONTAINER_HANDLE;
    width = 0;
    break;
  case TALOS_RT_DESIGN_SEMANTIC_UNPACKED_ARRAY:
  case TALOS_RT_DESIGN_SEMANTIC_UNPACKED_STRUCT:
  case TALOS_RT_DESIGN_SEMANTIC_UNPACKED_UNION:
    kind = TALOS_RT_ELEMENT_AGGREGATE;
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
      ((element->flags & TALOS_RT_ELEMENT_FOUR_STATE) != 0) != fourState)
    return TALOS_RT_INVALID_ARGUMENT;
  TALOS_RT_TRY {
    TraceSlots expected;
    status = expectedTrace(context, owner, semantic, size, expected);
    if (status != TALOS_RT_OK)
      return status;
    return matchingTrace(element->trace, expected)
               ? TALOS_RT_OK
               : TALOS_RT_INVALID_ARGUMENT;
  }
  TALOS_RT_CATCH(const std::bad_alloc &) { return TALOS_RT_OUT_OF_MEMORY; }
  TALOS_RT_CATCH_ALL { return TALOS_RT_OUT_OF_RESOURCES; }
}
